#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <pthread.h>

const int MAX_PORTS = 5;
const int PORT_START = 8080;
const int PORT_END = 8084;
const int MAX_FILES = 100;
const int MAX_FILENAME_LENGTH = 256;


//global variables
typedef struct {
    int port;
    int folder_id;
    char folder_path[256];
    int socket_fd;
    int is_bound;
    pthread_t thread_id;
    int thread_index;
} port_thread_data_t;

port_thread_data_t port_threads[MAX_PORTS];
int bound_port_count = 0;
char unique_files[MAX_FILES][MAX_FILENAME_LENGTH]; //this will store the unique files
int unique_file_count = 0; //this will count the unique files

pthread_mutex_t bound_ports_mutex = PTHREAD_MUTEX_INITIALIZER; 
pthread_mutex_t file_list_mutex = PTHREAD_MUTEX_INITIALIZER;

void setup_socket_addr(struct sockaddr_in* addr, int port) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addr->sin_port = htons(port);
}

//Checks if a port is available by attempting a temporary bind
int is_port_available(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 0;
    }

    struct sockaddr_in addr;
    setup_socket_addr(&addr, port);

    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr)); 
    close(sock);

    return result == 0;
}

//This will permanently bind to the port and starts listening
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

//this will help prevent duplicates
void add_unique_file(const char* filename) {
    pthread_mutex_lock(&file_list_mutex);
    
    for (int i = 0; i < unique_file_count; i++) {
        if (strcmp(unique_files[i], filename) == 0) {
            pthread_mutex_unlock(&file_list_mutex);
            return; //return to unlocks if it sees a duplicate
        }
    }
    
    if (unique_file_count < MAX_FILES) {
        strcpy(unique_files[unique_file_count], filename);
        unique_file_count++; //now we can add the file to the list
    }
    
    pthread_mutex_unlock(&file_list_mutex);
}

//this will scan the folder and add it to the global unique files list
void scan_folder_files(const char* folder_path) {
    DIR *dr = opendir(folder_path);
    if (dr == NULL) {
        printf("Could not open directory %s\n", folder_path);
        return;
    }
    
    struct dirent *de;
    printf("Scanning folder %s... ", folder_path);
    
    int files_found = 0;
    while ((de = readdir(dr)) != NULL) {
        if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
            add_unique_file(de->d_name);
            files_found++;
        }
    }
    
    printf("found %d file(s)\n", files_found);
    closedir(dr);
}

void scan_all_folders() {
    int total_files = 0;
    
    for (int i = 1; i <= MAX_PORTS; i++) {
        char folder_path[256];
        snprintf(folder_path, sizeof(folder_path), "files/%d", i);
        
        DIR *dr = opendir(folder_path);
        if (dr != NULL) {
            struct dirent *de;
            int files_found = 0;
            
            while ((de = readdir(dr)) != NULL) {
                if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
                    add_unique_file(de->d_name);
                    files_found++;
                    total_files++;
                }
            }
            closedir(dr);
        }
    }
}

/*
 * Thread function that handles a single port.
 * Tests if port is available, binds to it, and keeps it active.
 * Each thread is responsible for one port and its associated folder.
 */
 /*
void* port_thread_function(void* arg) {
    // Cast the generic pointer back to our thread data structure
    port_thread_data_t* thread_data = (port_thread_data_t*)arg;
    
    // Print which port we're trying to use
    printf("[Thread %d] Trying port %d... ", thread_data->thread_index, thread_data->port);
    
    // First check if the port is available
    if (is_port_available(thread_data->port)) {
        printf("available, attempting to bind... ");
        
        // If available, try to permanently bind and listen
        int sock = bind_and_listen(thread_data->port);
        if (sock >= 0) {
            // Success! Update thread data with connection info
            thread_data->socket_fd = sock;
            thread_data->is_bound = 1;
            
            // Print success messages
            printf("SUCCESS!\n");
            printf("[Thread %d] Bound to port %d, assigned folder %s\n", 
                   thread_data->thread_index, thread_data->port, thread_data->folder_path);
            
            // Thread-safely increment bound port counter
            pthread_mutex_lock(&bound_ports_mutex);
            bound_port_count++;
            pthread_mutex_unlock(&bound_ports_mutex);
            
            // Scan this thread's folder for files
            scan_folder_files(thread_data->folder_path);
            
            // Keep thread alive (this is where client handling would go)
            while (1) {
                sleep(1);
            }
        } else {
            // Binding failed (port might have been taken by another process)
            printf("failed to bind.\n");
            thread_data->is_bound = 0;
        }
    } else {
        // Port is already in use
        printf("in use.\n");
        thread_data->is_bound = 0;
    }
    
    return NULL;
} */

int scanning_active = 1;
pthread_mutex_t scanner_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 
 * Thread function that tries ports sequentially (8080-8084) until it finds one available.
 * When a port is found, it binds to it and scans all folders for files.
 * This is the main server initialization function.
 */
void* sequential_port_scanner(void* arg) {
    // Try each port in sequence (8080, 8081, 8082, 8083, 8084)
    for (int i = 0; i < MAX_PORTS; i++) {
        int port = PORT_START + i;
        
        // First check if port is available (quick test)
        if (is_port_available(port)) {
            // If available, try to permanently bind and listen
            int sock = bind_and_listen(port);
            if (sock >= 0) {
                // Success! Store all the port information in the global array
                port_threads[0].port = port;                // Which port we're using
                port_threads[0].folder_id = i + 1;          // Maps to folder (files/1, files/2, etc.)
                port_threads[0].thread_index = 0;           // This is thread #0
                port_threads[0].socket_fd = sock;           // Save socket descriptor for later
                port_threads[0].is_bound = 1;               // Mark as successfully bound
                
                // Create the folder path string (e.g., "files/1")
                snprintf(port_threads[0].folder_path, sizeof(port_threads[0].folder_path), 
                         "files/%d", port_threads[0].folder_id);
                
                // Thread-safely update the bound port counter
                pthread_mutex_lock(&bound_ports_mutex);
                bound_port_count = 1;
                pthread_mutex_unlock(&bound_ports_mutex);
                
                // Scan ALL folders for files, not just the one we're bound to
                scan_all_folders();
                
                // Stop after finding first available port
                break;
            }
        }
    }
    
    return NULL;
}

/*
 * Initializes all thread data and starts the port scanner thread.
 * This is the main function that sets up the server.
 */
void start_sequential_scanner() {
    printf("Finding available ports...");
    fflush(stdout);  // Ensure message is displayed immediately
    
    // Initialize all thread data with default values
    for (int i = 0; i < MAX_PORTS; i++) {
        port_threads[i].port = -1;           // No port assigned yet
        port_threads[i].folder_id = -1;      // No folder assigned yet
        port_threads[i].thread_index = i;    // Give each thread its index
        port_threads[i].socket_fd = -1;      // No socket connection yet
        port_threads[i].is_bound = 0;        // Not bound to any port yet
    }
    
    // Create a new thread to run the port scanner
    pthread_t scanner_thread;
    if (pthread_create(&scanner_thread, NULL, sequential_port_scanner, NULL) != 0) {
        printf("Failed to create scanner thread\n");
        return;
    }
    
    // Wait for the scanner thread to complete before continuing
    pthread_join(scanner_thread, NULL);
}

/*
 * Lists all unique files found in all folders (files/1 through files/5).
 * This function is called when the user selects option 1 from the menu.
 */
void listAvailableFiles() {
    printf("Searching for files... ");
    
    // Thread-safely reset the file counter
    pthread_mutex_lock(&file_list_mutex);
    unique_file_count = 0;
    pthread_mutex_unlock(&file_list_mutex);
    
    // Check each possible folder (files/1, files/2, files/3, files/4, files/5)
    for (int i = 1; i <= MAX_PORTS; i++) {
        // Create the folder path string (e.g., "files/1")
        char folder_path[256];
        snprintf(folder_path, sizeof(folder_path), "files/%d", i);
        
        // Try to open the directory
        DIR *dr = opendir(folder_path);
        if (dr != NULL) {
            struct dirent *de;
            // Read each entry in the directory
            while ((de = readdir(dr)) != NULL) {
                // Skip special directory entries "." and ".."
                if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
                    // Add this file to our global list (if not already there)
                    add_unique_file(de->d_name);
                }
            }
            closedir(dr);
        }
    }
    printf("Done.\n");
    
    // Thread-safely display the list of files
    pthread_mutex_lock(&file_list_mutex);
    if (unique_file_count == 0) {
        printf("No files found.\n");
    } else {
        // Print each file with a number
        for (int i = 0; i < unique_file_count; i++) {
            printf("[%d] %s\n", i + 1, unique_files[i]);
        }
    }
    pthread_mutex_unlock(&file_list_mutex);
}

/*
void download_file() {
    // First check if we have any files in our list
    if (unique_file_count == 0) {
        printf("No files available. Please list files first (option 1).\n");
        return;
    }
    
    // Get user's file selection
    int file_id;
    printf("\nEnter file ID: ");
    scanf("%d", &file_id);
    
    // Validate selection
    if (file_id < 1 || file_id > unique_file_count) {
        printf("Invalid file ID.\n");
        return;
    }
    
    // Get selected filename
    char selected_file[MAX_FILENAME_LENGTH];
    pthread_mutex_lock(&file_list_mutex);
    strcpy(selected_file, unique_files[file_id - 1]);
    pthread_mutex_unlock(&file_list_mutex);
    
    printf("Locating seeders... ");
    
    
    
    
    // Show download started message with exact format
    printf("Download started. File: [%d] %s\n", file_id, selected_file);
   
}
    */

void show_menu() {
    int choice;
    do {
        printf("\nSeed App\n");
        printf("[1] List available files.\n");
        printf("[2] Download file.\n");
        printf("[3] Download status.\n");
        printf("[4] Exit.\n");
        printf("\n ? ");

        scanf("%d", &choice);
        printf("\n");

        switch (choice) {
            case 1:
                listAvailableFiles();
                break;
            case 2:
                // download_file();  
                break;
            case 3:
                printf("Checking download status...\n");
                break;
            case 4:
                printf("Exiting...\n");
                break;
            default:
                printf("Invalid choice. Please try again.\n");
        }
    } while (choice != 4);
}

int main() {
    bound_port_count = 0;
    unique_file_count = 0;
    scanning_active = 1;
    
    start_sequential_scanner();
    
    if (bound_port_count > 0 && port_threads[0].is_bound) {
        printf(" Found port %d.\n", port_threads[0].port);
        printf("Listening at port %d.\n", port_threads[0].port);
    }
    
    show_menu();
    
    if (port_threads[0].is_bound && port_threads[0].socket_fd >= 0) {
        close(port_threads[0].socket_fd);
    }
    
    pthread_mutex_destroy(&bound_ports_mutex);
    pthread_mutex_destroy(&file_list_mutex);
    pthread_mutex_destroy(&scanner_mutex);
    
    return 0;
}
