#include <iostream>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <vector>
#include <iomanip>
#include <fstream>
#include <sstream>

#include <thread>

// Port configuration
const int PORTS[] = {8080, 8081, 8082, 8083, 8084};
const int MAX_PORTS = 5;
const int MAX_FILES = 100;
const int MAX_FILENAME_LENGTH = 256;

const int delay = 5000; //microseconds

// Global variables
typedef struct {
    int port;
    int folder_id;
    char folder_path[256];
    int socket_FileHandle;
    int is_bound;
    pthread_t thread_id;
    int thread_index;
} port_thread_data_t;

typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    int source_port;
} file_info_t;

port_thread_data_t port_threads[MAX_PORTS];
int bound_port_count = 0;
file_info_t unique_files[MAX_FILES];
int unique_file_count = 0;
int my_bound_port = -1;

pthread_mutex_t file_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t downloads_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    long long total_size;
    long long downloaded_bytes;
    bool completed;
} download_status_t;

std::vector<download_status_t> g_download_status;

// Function prototypes
void setup_socket_addr(struct sockaddr_in* addr, int port);
int create_directory(const char* path);
void scan_seeds_for_file(const char* filename, std::vector<int>& available_seeds);
long long get_file_size_from_seed(int port, const char* filename);
bool check_file_already_exists(const char* filename, long long expected_size, char* existing_path, size_t path_size);
void multi_seed_dl(const char* filename, const std::vector<int>& available_seeds);
void print_download_status();
void register_download_status(const char* filename, long long total_size);
void add_download_progress(const char* filename, long long bytes);
void complete_download_status(const char* filename);

void print_download_status() {
    pthread_mutex_lock(&downloads_mutex);
    if (g_download_status.empty()) {
        std::cout << "\nDownload status:\n(none)\n" << std::endl;
        pthread_mutex_unlock(&downloads_mutex);
        return;
    }
    std::cout << "\nDownload status:" << std::endl;
    for (size_t i = 0; i < g_download_status.size(); ++i) {
        const auto &d = g_download_status[i];
        double pct = (d.total_size > 0) ? (100.0 * (double)d.downloaded_bytes / (double)d.total_size) : 0.0;
        std::cout << "[" << (i + 1) << "] " << d.filename << " "
                  << d.downloaded_bytes / 1024 << "kb/" << d.total_size / 1024 << "kb ("
                  << std::fixed << std::setprecision(2) << pct << "%)" << std::endl;
    }
    std::cout << std::endl;
    pthread_mutex_unlock(&downloads_mutex);
}

void register_download_status(const char* filename, long long total_size) {
    pthread_mutex_lock(&downloads_mutex);
    for (auto &d : g_download_status) {
        if (strcmp(d.filename, filename) == 0) {
            d.total_size = total_size;
            d.completed = false;
            d.downloaded_bytes = 0;
            pthread_mutex_unlock(&downloads_mutex);
            return;
        }
    }
    download_status_t d{};
    strncpy(d.filename, filename, MAX_FILENAME_LENGTH - 1);
    d.filename[MAX_FILENAME_LENGTH - 1] = '\0';
    d.total_size = total_size;
    d.downloaded_bytes = 0;
    d.completed = false;
    g_download_status.push_back(d);
    pthread_mutex_unlock(&downloads_mutex);
}

void add_download_progress(const char* filename, long long bytes) {
    pthread_mutex_lock(&downloads_mutex);
    for (auto &d : g_download_status) {
        if (strcmp(d.filename, filename) == 0) {
            d.downloaded_bytes += bytes;
            if (d.downloaded_bytes > d.total_size) d.downloaded_bytes = d.total_size;
            break;
        }
    }
    pthread_mutex_unlock(&downloads_mutex);
}

void complete_download_status(const char* filename) {
    pthread_mutex_lock(&downloads_mutex);
    for (auto &d : g_download_status) {
        if (strcmp(d.filename, filename) == 0) {
            d.downloaded_bytes = d.total_size;
            d.completed = true;
            break;
        }
    }
    pthread_mutex_unlock(&downloads_mutex);
}

// Data for parallel download workers
typedef struct {
    const char* filename;
    std::vector<int> seeds;
    int chunk_size;
    long long total_size;
    int my_folder_id;
    char download_path[1024];
    int first_source_folder_id;
    int fd; // POSIX file descriptor for pwrite
    pthread_mutex_t queue_mutex;
    long long next_offset; // next offset to claim
    long long bytes_downloaded;
    int chunks_downloaded;
    bool stop;
} parallel_ctx_t;

typedef struct {
    parallel_ctx_t* ctx;
    int seed_index;
} worker_arg_t;

void setup_socket_addr(struct sockaddr_in* addr, int port) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addr->sin_port = htons(port);
}

int create_directory(const char* path) {
    char temp[1024];
    char* pos = NULL;
    
    if (strlen(path) >= sizeof(temp)) {
        return -1;
    }
    
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    auto len = strlen(temp);
    
    if (len > 0 && temp[len - 1] == '/') {
        temp[len - 1] = '\0';
        len--;
    }
    
    for (pos = temp + 1; *pos; pos++) {
        if (*pos == '/') {
            *pos = '\0';
            if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *pos = '/';
        }
    }
    
    if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    
    return 0;
}

int bind_and_listen(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    setup_socket_addr(&addr, port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    if (listen(sock, 5) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

void add_unique_file(const char* filename, int source_port) {
    pthread_mutex_lock(&file_list_mutex);
    
    for (auto i = 0; i < unique_file_count; i++) {
        if (strcmp(unique_files[i].filename, filename) == 0) {
            pthread_mutex_unlock(&file_list_mutex);
            return;
        }
    }
    
    if (unique_file_count < MAX_FILES) {
        if (strlen(filename) < MAX_FILENAME_LENGTH) {
            strncpy(unique_files[unique_file_count].filename, filename, MAX_FILENAME_LENGTH - 1);
            unique_files[unique_file_count].filename[MAX_FILENAME_LENGTH - 1] = '\0';
            unique_files[unique_file_count].source_port = source_port;
            unique_file_count++;
        }
    }
    pthread_mutex_unlock(&file_list_mutex);
}

void get_own_files(char* response, int max_size) {
    auto my_folder_id = -1;

    for (auto i = 0; i < MAX_PORTS; i++) {
        if (PORTS[i] == my_bound_port) {
            my_folder_id = i + 1;
            break;
        }
    }
    
    if (my_folder_id == -1) {
        response[0] = '\0';
        return;
    }
    
    char folder_path[256];
    snprintf(folder_path, sizeof(folder_path), "files/seed%d/%d", my_folder_id, my_folder_id);
    
    response[0] = '\0';
    DIR *dr = opendir(folder_path);
    if (dr != NULL) {
        struct dirent *de;
        auto file_count = 0;
        
        while ((de = readdir(dr)) != NULL) {
            if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
                char file_entry[512];
                snprintf(file_entry, sizeof(file_entry), "[%d] %s\n", ++file_count, de->d_name);
                
                size_t current_len = strlen(response);
                size_t entry_len = strlen(file_entry);
                if (current_len + entry_len < (size_t)max_size - 1) {
                    strncat(response, file_entry, max_size - current_len - 1);
                } else {
                    break;
                }
            }
        }
        closedir(dr);
    }
}

void* port_request(void* arg) {
    auto client_filehandle = *(int*)arg;
    free(arg);
    
    char buffer[1024];
    auto bytes = recv(client_filehandle, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        close(client_filehandle);
        return NULL;
    }
    buffer[bytes] = '\0';

    if (strcmp(buffer, "LIST") == 0) {
        char response[2048];
        get_own_files(response, sizeof(response));
        send(client_filehandle, response, strlen(response), 0);
    }
    else if (strncmp(buffer, "FILESIZE ", 9) == 0) {
        char filename[MAX_FILENAME_LENGTH];
        size_t filename_len = strlen(buffer + 9);
        if (filename_len < MAX_FILENAME_LENGTH) {
            strncpy(filename, buffer + 9, MAX_FILENAME_LENGTH - 1);
            filename[MAX_FILENAME_LENGTH - 1] = '\0';
        } else {
            char error_msg[] = "Filename too long";
            send(client_filehandle, error_msg, strlen(error_msg), 0);
            close(client_filehandle);
            return NULL;
        }
        
        auto newline = strchr(filename, '\n');
        if (newline) *newline = '\0';
        
        auto my_folder_id = -1;
        for (auto i = 0; i < MAX_PORTS; i++) {
            if (PORTS[i] == my_bound_port) {
                my_folder_id = i + 1;
                break;
            }
        }
        
        if (my_folder_id != -1) {
            char file_path[1024];
            snprintf(file_path, sizeof(file_path), "files/seed%d/%d/%s", my_folder_id, my_folder_id, filename);
            
            FILE *file = fopen(file_path, "rb");
            if (file) {
                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                fclose(file);
                
                char size_response[64];
                snprintf(size_response, sizeof(size_response), "SIZE:%ld", file_size);
                send(client_filehandle, size_response, strlen(size_response), 0);
            } else {
                char error_msg[] = "File not found";
                send(client_filehandle, error_msg, strlen(error_msg), 0);
            }
        }
    }
    else if (strncmp(buffer, "DOWNLOAD ", 9) == 0) {
        char filename[MAX_FILENAME_LENGTH];
        long long offset = 0;
        
        char* delimiter_pos = strchr(buffer + 9, '|');
        if (delimiter_pos) {
            size_t filename_len = delimiter_pos - (buffer + 9);
            if (filename_len < MAX_FILENAME_LENGTH) {
                strncpy(filename, buffer + 9, filename_len);
                filename[filename_len] = '\0';
                offset = atoll(delimiter_pos + 1);
            } else {
                char error_msg[] = "Filename too long";
                send(client_filehandle, error_msg, strlen(error_msg), 0);
                close(client_filehandle);
                return NULL;
            }
        } else {
            size_t filename_len = strlen(buffer + 9);
            if (filename_len < MAX_FILENAME_LENGTH) {
                strncpy(filename, buffer + 9, MAX_FILENAME_LENGTH - 1);
                filename[MAX_FILENAME_LENGTH - 1] = '\0';
            } else {
                char error_msg[] = "Filename too long";
                send(client_filehandle, error_msg, strlen(error_msg), 0);
                close(client_filehandle);
                return NULL;
            }
        }
        
        auto newline = strchr(filename, '\n');
        if (newline) *newline = '\0';
        
        auto my_folder_id = -1;
        for (auto i = 0; i < MAX_PORTS; i++) {
            if (PORTS[i] == my_bound_port) {
                my_folder_id = i + 1;
                break;
            }
        }
        
        if (my_folder_id != -1) {
            char file_path[1024];
            snprintf(file_path, sizeof(file_path), "files/seed%d/%d/%s", my_folder_id, my_folder_id, filename);
            
            FILE *file = fopen(file_path, "rb");
            if (file) {
                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                fseek(file, 0, SEEK_SET); //try wagtanga
                
                if (offset >= file_size) {
                    fclose(file);
                    close(client_filehandle);
                    return NULL;
                }
                
                fseek(file, offset, SEEK_SET);
                
                //sends 32 bytes to the client
                char file_buffer[32];
                size_t bytes_read = fread(file_buffer, 1, sizeof(file_buffer), file);
                
                if (bytes_read > 0) {
                    send(client_filehandle, file_buffer, bytes_read, 0);
                }
                
                fclose(file);
            } else {
                char error_msg[] = "File not found";
                send(client_filehandle, error_msg, strlen(error_msg), 0);
            }
        }
    }
    
    close(client_filehandle);
    return NULL;
}

void* server_thread(void* arg) {
    auto server_filehandle = *(int*)arg;
    free(arg);
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (1) {
        auto client_filehandle = accept(server_filehandle, (struct sockaddr*)&client_addr, &client_len);
        if (client_filehandle >= 0) {
            auto client_filehandle_ptr = (int*)malloc(sizeof(int));
            if (client_filehandle_ptr == NULL) {
                close(client_filehandle);
                continue;
            }
          
            *client_filehandle_ptr = client_filehandle;
            
            pthread_t handler_thread;
            pthread_create(&handler_thread, NULL, port_request, client_filehandle_ptr);
            pthread_detach(handler_thread);
        }
    }
    return NULL;
}


void port_server() {
    std::cout << "Finding available ports...";
  
    for (auto i = 0; i < MAX_PORTS; i++) {
        auto port = PORTS[i];
        auto server_filehandle = bind_and_listen(port); //change this sock variable to server_handle

        if (server_filehandle >= 0) {
            my_bound_port = port;
            
            port_threads[0].port = port;
            port_threads[0].folder_id = i + 1;
            port_threads[0].thread_index = 0;
            port_threads[0].socket_FileHandle = server_filehandle;
            port_threads[0].is_bound = 1;
            snprintf(port_threads[0].folder_path, sizeof(port_threads[0].folder_path), 
                     "files/seed%d/%d", port_threads[0].folder_id, port_threads[0].folder_id);
            
            bound_port_count = 1;
            
            std::cout << " Found port " << port << "." << std::endl;
            std::cout << "Listening at port " << port << "." << std::endl;

            auto server_filehandle_ptr = (int*)malloc(sizeof(int));  
            
            if (server_filehandle_ptr == NULL) {
                close(server_filehandle);
                return;
            }
            
            *server_filehandle_ptr = server_filehandle;
            pthread_t server_tid;
            pthread_create(&server_tid, NULL, server_thread, server_filehandle_ptr);
            pthread_detach(server_tid);
            
            return;
        }
    }
    
    std::cout << " No available ports found." << std::endl;
}

void download_file() {
    pthread_mutex_lock(&file_list_mutex);
    if (unique_file_count == 0) {
        std::cout << "No files available to download. Please list files first (option 1)." << std::endl;
        pthread_mutex_unlock(&file_list_mutex);
        return;
    }
     
    std::cout << "Available files for download:" << std::endl;
    for (auto i = 0; i < unique_file_count; i++) {
        std::cout << "[" << i + 1 << "] " << 
               unique_files[i].filename << " (from seed at port " << unique_files[i].source_port << ")" << std::endl;
    }
     
    std::cout << "\nEnter file ID: ";
    auto file_choice = 0;
    std::cin >> file_choice;
     
    if (file_choice < 1 || file_choice > unique_file_count) {
        std::cout << "Locating seeders... Failed" << std::endl;
        std::cout << "No seeders for file ID " << file_choice << "." << std::endl;
        pthread_mutex_unlock(&file_list_mutex);
        return;
    }
     
    char filename[MAX_FILENAME_LENGTH];
    size_t filename_len = strlen(unique_files[file_choice - 1].filename);
    if (filename_len < MAX_FILENAME_LENGTH) {
        strncpy(filename, unique_files[file_choice - 1].filename, MAX_FILENAME_LENGTH - 1);
        filename[MAX_FILENAME_LENGTH - 1] = '\0';
    } else {
        std::cout << "Filename too long." << std::endl;
        pthread_mutex_unlock(&file_list_mutex);
        return;
    }
    pthread_mutex_unlock(&file_list_mutex);
     
    std::cout << "Scanning all seeds for file '" << filename << "'..." << std::endl;
     
    std::vector<int> available_seeds;
    scan_seeds_for_file(filename, available_seeds);
     
    if (available_seeds.empty()) {
        std::cout << "No seeds found with file '" << filename << "'. Cannot download." << std::endl;
        return;
    }
     
    auto expected_size = get_file_size_from_seed(available_seeds[0], filename);
    if (expected_size <= 0) {
        std::cout << "Could not determine file size. Download may fail." << std::endl;
    } else {
        char existing_path[1024];
        if (check_file_already_exists(filename, expected_size, existing_path, sizeof(existing_path))) {
            std::cout << " File [" << file_choice << "] " << filename << " already exists" << std::endl;
            return;
        } else {
            std::cout << "Starting download..." << std::endl;
        }
    }
     
    std::cout << "Starting download for file: " << filename << std::endl;
    
    //  multi_seed_dl(filename, available_seeds);

     std::thread([fn = std::string(filename), seeds = available_seeds]() {
        multi_seed_dl(fn.c_str(), seeds);
    }).detach();
    std::cout << "Download started in background." << std::endl;
}

void scan_seeds_for_file(const char* filename, std::vector<int>& available_seeds) {
    available_seeds.clear();
    
    for (auto i = 0; i < MAX_PORTS; i++) {
        auto port = PORTS[i];
        if (port != my_bound_port) {
            std::cout << "Scanning seed at port " << port << "... ";
            
            auto sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                std::cout << "failed" << std::endl;
                continue;
            }
            
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            
            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) { //connection to the server
                send(sock, "LIST", strlen("LIST"), 0); //send the request list of files to the server
                
                char buffer[1024];
                auto n = recv(sock, buffer, sizeof(buffer) - 1, 0); //reciv data from the server then store in the buffer
                if (n > 0) {
                    buffer[n] = '\0';
                    
                    
                    bool file_found = false;
                    char *saveptr;
                    char *line = strtok_r(buffer, "\n", &saveptr); 
                    while (line != NULL) {
                        char *bracket_end = strchr(line, ']');
                        if (bracket_end && bracket_end[1] == ' ') {
                            char *seed_filename = bracket_end + 2;
                            if (strcmp(seed_filename, filename) == 0) {
                                file_found = true; 
                                break;
                            }
                        }
                        line = strtok_r(NULL, "\n", &saveptr);
                    }
                                        
                    if (file_found) {
                        std::cout << "found" << std::endl;
                        available_seeds.push_back(port); //file found added to the available_seeds list
                    } else {
                        std::cout << "not found" << std::endl;
                    }
                } else {
                    std::cout << "no response" << std::endl;
                }
            } else {
                std::cout << "not running" << std::endl;
            }
            
            close(sock);
        }
    }
    
    std::cout << "Found " << available_seeds.size() << " seeds with file '" << filename << "'" << std::endl;
}

void multi_seed_dl(const char* filename, const std::vector<int>& available_seeds) {
    if (available_seeds.empty()) {
        std::cout << "No seed available for this file." << std::endl;
        return;
    }

    const int CHUNK_SIZE = 32;
    auto total_seeds = (int)available_seeds.size();

     // Resolve my folder id (used to build the destination path)
    auto my_folder_id = -1;
    for (auto i = 0; i < MAX_PORTS; i++) {
        if (PORTS[i] == my_bound_port) { my_folder_id = i + 1; break; }
    }
 
    //this will find the file path of the first seeder/chunks downloaded
    auto first_source_folder_id = -1;
    for (auto i = 0; i < MAX_PORTS; i++) {
        if (PORTS[i] == available_seeds[0]) { first_source_folder_id = i + 1; break; }
    }

    // Determine total size (fallback to 10KB)
    auto total_size = get_file_size_from_seed(available_seeds[0], filename);
    if (total_size <= 0) {
        total_size = CHUNK_SIZE * 320; // 10 KB fallback
    }
    long long chunks = (total_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    // register status entry
    register_download_status(filename, total_size);

     char download_dir[1024];
    snprintf(download_dir, sizeof(download_dir), "files/seed%d/%d/%d", my_folder_id, my_folder_id, first_source_folder_id);
    if (create_directory(download_dir) != 0) {
        std::cout << "Could not create directory " << download_dir << std::endl;
    }
    char download_path[1024];

    auto path_result = snprintf(download_path, sizeof(download_path), "%s/%s", download_dir, filename);
    if (path_result >= (int)sizeof(download_path)) {
        std::cout << "File path too long, cannot download." << std::endl;
        return;
    }

    // seeder function
    auto seeder_fn = [](void* arg) -> void* {
        worker_arg_t* s = (worker_arg_t*)arg;
        int seed_idx = s->seed_index;
        int seed_port = s->ctx->seeds[seed_idx];
        const char* filename = s->ctx->filename;
        long long chunk_size = s->ctx->chunk_size;
        long long total_size = s->ctx->total_size;
        long long chunks = (total_size + chunk_size - 1) / chunk_size;

        // make a temporary file where the seeder stores the chunks downloaded
        std::string part_filename = std::string("seeders/") + filename + ".part-" + std::to_string(seed_idx);
        FILE* part_file = fopen(part_filename.c_str(), "wb");
        if (!part_file) { return NULL; }

        // Download the chunks assigned to this seeder
        for (long long i = seed_idx; i < chunks; i += s->ctx->seeds.size()) {
            long long offset = i * chunk_size;
            long long this_chunk_size = std::min(chunk_size, total_size - offset); //this will check if thare are atleast chunks size bytes left be downloaded

            // Connect to seed
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) continue;
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = htons(seed_port);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) { close(sock); continue; }

            // Request this chunk
            char request[512];
            snprintf(request, sizeof(request), "DOWNLOAD %s|%lld", filename, offset);
            send(sock, request, strlen(request), 0);

            // request sent

            // Read chunk
            std::vector<char> buffer(this_chunk_size);
            int total_read_chunks = 0;
            while (total_read_chunks < this_chunk_size) {
                int bytes_from_sockets = recv(sock, buffer.data() + total_read_chunks, this_chunk_size - total_read_chunks, 0);
                if (bytes_from_sockets <= 0) break;
                total_read_chunks += bytes_from_sockets;
            }
            close(sock);

            if (total_read_chunks > 0) { fwrite(buffer.data(), 1, total_read_chunks, part_file); add_download_progress(filename, total_read_chunks); }

            // throttle between chunk requests
            usleep(delay);
        }
        fclose(part_file);
        // worker finished
        delete s;
        return NULL;
    };

    // Ensure part files directory exists
    // create_directory("seeders");

    // Launch one thread per seed
    std::vector<pthread_t> tids(total_seeds);
    parallel_ctx_t ctx{};
    ctx.filename = filename;
    ctx.seeds = available_seeds;
    ctx.chunk_size = CHUNK_SIZE;
    ctx.total_size = total_size;

    for (int i = 0; i < total_seeds; i++) {
        worker_arg_t* s = new worker_arg_t{ &ctx, i };
        pthread_create(&tids[i], NULL, seeder_fn, s);
    }

    // Join threads
    for (int i = 0; i < total_seeds; i++) {
        pthread_join(tids[i], NULL);
    }

    // Merge part files
    FILE* final_file = fopen(download_path, "wb");
    if (!final_file) {
        std::cout << "Failed to open final file for write." << std::endl;
        return;
    }

    // Assemble final file from part files
    for (long long chunk = 0; chunk < chunks; ++chunk) {
        int seed_idx = chunk % total_seeds;
        std::string part_filename = std::string("seeders/") + filename + ".part-" + std::to_string(seed_idx);
        FILE* part_file = fopen(part_filename.c_str(), "rb");
        if (!part_file) {
            continue;
        }
        long long offset_in_part = (chunk / total_seeds) * CHUNK_SIZE;
        fseek(part_file, offset_in_part, SEEK_SET);
        long long this_chunk_size = std::min((long long)CHUNK_SIZE, total_size - chunk * CHUNK_SIZE);
        std::vector<char> buffer(this_chunk_size);
        size_t bytes = fread(buffer.data(), 1, this_chunk_size, part_file);
        fwrite(buffer.data(), 1, bytes, final_file);
        fclose(part_file);
    }
    fclose(final_file);

    // Post-merge statistics per seed (by port)
    std::vector<long long> bytes_by_seed(total_seeds, 0);
    std::vector<long long> chunks_by_seed(total_seeds, 0);
    long long total_downloaded_bytes = 0;
    // mark as completed
    complete_download_status(filename);
    for (int seed_idx = 0; seed_idx < total_seeds; ++seed_idx) {
        std::string part_filename = std::string("seeders/") + filename + ".part-" + std::to_string(seed_idx);
        FILE* pf = fopen(part_filename.c_str(), "rb");
        if (!pf) continue;
        fseek(pf, 0, SEEK_END);
        long long sz = ftell(pf);
        fclose(pf);
        bytes_by_seed[seed_idx] = sz;
        chunks_by_seed[seed_idx] = (sz + CHUNK_SIZE - 1) / CHUNK_SIZE;
        total_downloaded_bytes += sz;
    }
}


bool check_file_already_exists(const char* filename, long long expected_size, char* existing_path, size_t path_size) {
    auto my_folder_id = -1;
    for (auto i = 0; i < MAX_PORTS; i++) {
        if (PORTS[i] == my_bound_port) {
            my_folder_id = i + 1;
            break;
        }
    }
    
    if (my_folder_id == -1) {
        return false;
    }
    
   
    const char* possible_paths[] = {
        "files/seed%d/%d/%s",
        "files/seed%d/%d/1/%s",
        "files/seed%d/%d/2/%s",
        "files/seed%d/%d/3/%s",
        "files/seed%d/%d/4/%s",
        "files/seed%d/%d/5/%s"
    };
    
    for (size_t i = 0; i < sizeof(possible_paths) / sizeof(possible_paths[0]); i++) {
        char check_path[1024];
       
        snprintf(check_path, sizeof(check_path), possible_paths[i], my_folder_id, my_folder_id, filename);
        
        FILE* file = fopen(check_path, "rb");
        if (file) {
            fseek(file, 0, SEEK_END); 
            long long file_size = ftell(file);
            fclose(file); 
            
            if (file_size == expected_size) {
                if (strlen(check_path) < path_size) {
                    strcpy(existing_path, check_path);
                    return true;
                }
            }
        }
    }
    
    return false;
}


long long get_file_size_from_seed(int port, const char* filename) {
    auto sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(sock);
        return -1;
    }
    
    char request[512];
    snprintf(request, sizeof(request), "FILESIZE %s", filename);
    send(sock, request, strlen(request), 0);
    
    char buffer[64];
    auto bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    
    close(sock);
    
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        if (strncmp(buffer, "SIZE:", 5) == 0) {
            long long file_size = atoll(buffer + 5);
            return file_size;
        }
    }
    
    return -1;
}


void listAvailableFiles() {
    std::cout << "\nSearching for files... " << std::endl;
    
    pthread_mutex_lock(&file_list_mutex);
    unique_file_count = 0;
    pthread_mutex_unlock(&file_list_mutex);
    
    auto seeds_found = 0;
    
    for (auto i = 0; i < MAX_PORTS; i++) {
        auto port = PORTS[i];
        if (port != my_bound_port) {
            std::cout << "Trying to connect to port " << port << " ";
            
            auto sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                std::cout << "socket failed" << std::endl;
                continue;
            }
            
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            
            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                send(sock, "LIST", strlen("LIST"), 0);
                
                char buffer[1024];
                auto n = recv(sock, buffer, sizeof(buffer) - 1, 0);
                if (n > 0) {
                    buffer[n] = '\0';
                    std::cout << "connected, found files" << std::endl;
                    seeds_found++;
                    
                    char *saveptr2;
                    auto line = strtok_r(buffer, "\n", &saveptr2);
                    while (line != NULL) {
                        auto bracket_end = strchr(line, ']');
                        if (bracket_end && bracket_end[1] == ' ') {
                            add_unique_file(bracket_end + 2, port);
                        }
                        line = strtok_r(NULL, "\n", &saveptr2);
                    }
                } else {
                    std::cout << "no response" << std::endl;
                }
            } else {
                std::cout << "not running" << std::endl;
            }
            
            close(sock);
        }
    }
    
    std::cout << "done." << std::endl;
    
    pthread_mutex_lock(&file_list_mutex);
    if (unique_file_count == 0) {
        std::cout << "No files found from port instances." << std::endl;
    } else {
        std::cout << "Files available." << std::endl;
        for (auto i = 0; i < unique_file_count; i++) {
            std::cout << "[" << i + 1 << "] " << 
                unique_files[i].filename << " from port " << unique_files[i].source_port << std::endl;
        }
        std::cout << "\nFound files from " << seeds_found << " running ports" << std::endl;
    }
    pthread_mutex_unlock(&file_list_mutex);
}

void show_menu() {
    auto choice = 0;
    do {
        std::cout << "\nSeed App me\n";
        std::cout << "[1] List available files.\n";
        std::cout << "[2] Download file.\n";
        std::cout << "[3] Download status.\n";
        std::cout << "[4] Exit.\n";
        std::cout << "\n ? ";

        std::cin >> choice;
        std::cout << "\n";

        switch (choice) {
            case 1:
                listAvailableFiles();
                break;
            case 2:
                download_file();
                break;
            case 3:
                print_download_status();
                break;
            case 4:
                std::cout << "Exiting..." << std::endl;
                break;
            default:
                std::cout << "Invalid choice. Please try again." << std::endl;
        }
    } while (choice != 4);
}

int main() {
    bound_port_count = 0;
    unique_file_count = 0;
    my_bound_port = -1;
    
    port_server();
    
    if (my_bound_port == -1) {
        std::cout << "Could not bind to any port. Exiting." << std::endl;
        return 1;
    }
    
    show_menu();
    
    if (port_threads[0].is_bound && port_threads[0].socket_FileHandle >= 0) {
        close(port_threads[0].socket_FileHandle);
    }
    
    pthread_mutex_destroy(&file_list_mutex);
    pthread_mutex_destroy(&downloads_mutex);
    
    return 0;
}