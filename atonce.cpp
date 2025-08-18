// Standard C libraries for basic operations
#include <stdio.h>      // For printf, scanf
#include <stdlib.h>     // For memory allocation
#include <string.h>     // For string operations
#include <unistd.h>     // For sleep, close
// Socket programming libraries
#include <sys/socket.h> // For socket creation and binding
#include <netinet/in.h> // For internet address structures
#include <arpa/inet.h>  // For address conversion
// Directory operations and threading
#include <dirent.h>     // For directory scanning (opendir, readdir)
#include <pthread.h>    // For POSIX threads

// Program configuration constants
const int MAX_PORTS = 5;                    // Maximum number of ports to scan (8080-8084)
const int PORT_START = 8080;                // Starting port number
const int PORT_END = 8084;                  // Ending port number
const int MAX_FILES = 100;                  // Maximum files to store in list
const int MAX_FILENAME_LENGTH = 256;        // Maximum length of filename

// Data structure to store information for each port thread
typedef struct {
    int port;                    // Port number (8080, 8081, etc.)
    int folder_id;               // Corresponding folder ID (1, 2, 3, etc.)
    char folder_path[256];       // Path to folder (files/1, files/2, etc.)
    int socket_fd;               // Socket file descriptor for network connection
    int is_bound;                // Flag: 1 if port is successfully bound, 0 if not
    pthread_t thread_id;         // POSIX thread identifier
    int thread_index;            // Thread number (0, 1, 2, 3, 4)
} port_thread_data_t;

// Global arrays to store program data
port_thread_data_t port_threads[MAX_PORTS];              // Array of port thread data
int bound_port_count = 0;                                // Counter of successfully bound ports
char unique_files[MAX_FILES][MAX_FILENAME_LENGTH];       // Array to store unique filenames
int unique_file_count = 0;                               // Counter of unique files found

// POSIX mutex locks for thread-safe operations (prevents data corruption)
pthread_mutex_t bound_ports_mutex = PTHREAD_MUTEX_INITIALIZER;  // Protects bound port counter
pthread_mutex_t file_list_mutex = PTHREAD_MUTEX_INITIALIZER;    // Protects file list operations

// Function to test if a specific port is available for binding
// Returns: 1 if port is free, 0 if port is already in use
int is_port_available(int port) {
    // Create a temporary socket for testing
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 0;  // Failed to create socket
    }

    // Set up address structure for the port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));           // Clear the structure
    addr.sin_family = AF_INET;                // IPv4 address family
    addr.sin_addr.s_addr = INADDR_ANY;        // Accept connections from any IP
    addr.sin_port = htons(port);              // Convert port to network byte order

    // Try to bind to the port (this will fail if port is already in use)
    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);  // Close the temporary socket

    return result == 0;  // Return 1 if bind succeeded (port available), 0 if failed
}

// Function to permanently bind to a port and start listening for connections
// Returns: socket file descriptor on success, -1 on failure
int bind_and_listen(int port) {
    // Create a permanent socket for this port
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;  // Failed to create socket
    }

    // Set socket option to allow port reuse (prevents "Address already in use" errors)
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set up address structure for permanent binding
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));           // Clear the structure
    addr.sin_family = AF_INET;                // IPv4 address family
    addr.sin_addr.s_addr = INADDR_ANY;        // Accept connections from any IP
    addr.sin_port = htons(port);              // Convert port to network byte order

    // Permanently bind to the port
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;  // Binding failed
    }

    // Start listening for incoming connections (queue up to 5 connections)
    if (listen(sock, 5) < 0) {
        close(sock);
        return -1;  // Listen failed
    }

    return sock;  // Return socket file descriptor for future use
}

// Thread-safe function to add unique files to the global list (prevents duplicates)
// Multiple threads can call this simultaneously without data corruption
void add_unique_file(const char* filename) {
    // Lock mutex to ensure only one thread modifies file list at a time
    pthread_mutex_lock(&file_list_mutex);
    
    // Check if file already exists in the list (avoid duplicates)
    for (int i = 0; i < unique_file_count; i++) {
        if (strcmp(unique_files[i], filename) == 0) {
            pthread_mutex_unlock(&file_list_mutex);  // Release lock
            return; // File already exists, don't add duplicate
        }
    }
    
    // Add new unique file to the global list
    if (unique_file_count < MAX_FILES) {
        strcpy(unique_files[unique_file_count], filename);  // Copy filename
        unique_file_count++;  // Increment counter
    }
    
    // Release the lock so other threads can access the file list
    pthread_mutex_unlock(&file_list_mutex);
}

// Function to scan a directory and add all files to the unique file list
// Uses opendir/readdir to read directory contents
void scan_folder_files(const char* folder_path) {
    // Open the directory for reading
    DIR *dr = opendir(folder_path);
    if (dr == NULL) {
        printf("Could not open directory %s\n", folder_path);
        return;  // Exit if directory doesn't exist or can't be opened
    }
    
    struct dirent *de;  // Structure to hold directory entry information
    printf("Scanning folder %s... ", folder_path);
    
    int files_found = 0;
    // Read each entry in the directory
    while ((de = readdir(dr)) != NULL) {
        // Skip special directory entries "." (current dir) and ".." (parent dir)
        if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
            add_unique_file(de->d_name);  // Add filename to global list
            files_found++;
        }
    }
    
    printf("found %d file(s)\n", files_found);
    closedir(dr);  // Close the directory
}

// Thread function - each thread handles one port
void* port_thread_function(void* arg) {
    port_thread_data_t* thread_data = (port_thread_data_t*)arg;
    
    printf("[Thread %d] Trying port %d... ", thread_data->thread_index, thread_data->port);
    
    if (is_port_available(thread_data->port)) {
        printf("available, attempting to bind... ");
        
        int sock = bind_and_listen(thread_data->port);
        if (sock >= 0) {
            // Successfully bound
            thread_data->socket_fd = sock;
            thread_data->is_bound = 1;
            
            printf("SUCCESS!\n");
            printf("[Thread %d] Bound to port %d, assigned folder %s\n", 
                   thread_data->thread_index, thread_data->port, thread_data->folder_path);
            
            // Thread-safe increment of bound port count
            pthread_mutex_lock(&bound_ports_mutex);
            bound_port_count++;
            pthread_mutex_unlock(&bound_ports_mutex);
            
            // Scan files from this port's folder
            scan_folder_files(thread_data->folder_path);
            
            // Keep the socket open (in real implementation, handle client connections here)
            while (1) {
                sleep(1); // Keep thread alive
            }
        } else {
            printf("failed to bind.\n");
            thread_data->is_bound = 0;
        }
    } else {
        printf("in use.\n");
        thread_data->is_bound = 0;
    }
    
    return NULL;
}

// Global variables for port thread management
int scanning_active = 1;
pthread_mutex_t scanner_mutex = PTHREAD_MUTEX_INITIALIZER;

// Sequential port scanning - tries ports one by one until first success
void* sequential_port_scanner(void* arg) {
    for (int i = 0; i < MAX_PORTS; i++) {
        int port = PORT_START + i;
        
        if (is_port_available(port)) {
            int sock = bind_and_listen(port);
            if (sock >= 0) {
                // Successfully bound to first available port
                port_threads[0].port = port;
                port_threads[0].folder_id = i + 1;
                port_threads[0].thread_index = 0;
                port_threads[0].socket_fd = sock;
                port_threads[0].is_bound = 1;
                snprintf(port_threads[0].folder_path, sizeof(port_threads[0].folder_path), 
                         "files/%d", port_threads[0].folder_id);
                
                // Thread-safe increment
                pthread_mutex_lock(&bound_ports_mutex);
                bound_port_count = 1;
                pthread_mutex_unlock(&bound_ports_mutex);
                
                // Scan files from this port's folder
                DIR *dr = opendir(port_threads[0].folder_path);
                if (dr != NULL) {
                    struct dirent *de;
                    while ((de = readdir(dr)) != NULL) {
                        if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
                            add_unique_file(de->d_name);
                        }
                    }
                    closedir(dr);
                }
                
                // Stop after binding to first available port
                break;
            }
        }
    }
    
    return NULL;
}

// Creates single scanner thread that finds first available port
void start_sequential_scanner() {
    printf("Finding available ports...");
    fflush(stdout);
    
    // Initialize all port thread data
    for (int i = 0; i < MAX_PORTS; i++) {
        port_threads[i].port = -1;
        port_threads[i].folder_id = -1;
        port_threads[i].thread_index = i;
        port_threads[i].socket_fd = -1;
        port_threads[i].is_bound = 0;
    }
    
    // Create single scanner thread that finds first available port
    pthread_t scanner_thread;
    if (pthread_create(&scanner_thread, NULL, sequential_port_scanner, NULL) != 0) {
        printf("Failed to create scanner thread\n");
        return;
    }
    
    // Wait for scanner to complete
    pthread_join(scanner_thread, NULL);
}

// Thread-safe function to list all available files from bound ports
void listAvailableFiles() {
    printf("Searching for files... ");
    
    // Thread-safe clear of unique files list
    pthread_mutex_lock(&file_list_mutex);
    unique_file_count = 0;
    pthread_mutex_unlock(&file_list_mutex);
    
    // Rescan files from all bound ports (silently)
    for (int i = 0; i < MAX_PORTS; i++) {
        if (port_threads[i].is_bound) {
            DIR *dr = opendir(port_threads[i].folder_path);
            if (dr != NULL) {
                struct dirent *de;
                while ((de = readdir(dr)) != NULL) {
                    if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
                        add_unique_file(de->d_name);
                    }
                }
                closedir(dr);
            }
        }
    }
    printf("Files available.\n");
    
    pthread_mutex_lock(&file_list_mutex);
    if (unique_file_count == 0) {
        printf("No files found.\n");
    } else {
        for (int i = 0; i < unique_file_count; i++) {
            printf("[%d] %s\n", i + 1, unique_files[i]);
        }
    }
    pthread_mutex_unlock(&file_list_mutex);
}

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
                printf("Downloading file...\n");
                // Add your logic here
                break;
            case 3:
                printf("Checking download status...\n");
                // Add your logic here
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
    // Initialize global variables
    bound_port_count = 0;
    unique_file_count = 0;
    scanning_active = 1;
    
    // Start sequential scanner - finds first available port only
    start_sequential_scanner();
    
    // Show found port in the desired format
    if (bound_port_count > 0 && port_threads[0].is_bound) {
        printf(" Found port %d.\n", port_threads[0].port);
        printf("Listening at port %d.\n", port_threads[0].port);
    }
    
    // Show the menu
    show_menu();
    
    // Clean up
    if (port_threads[0].is_bound && port_threads[0].socket_fd >= 0) {
        close(port_threads[0].socket_fd);
    }
    
    // Destroy mutexes
    pthread_mutex_destroy(&bound_ports_mutex);
    pthread_mutex_destroy(&file_list_mutex);
    pthread_mutex_destroy(&scanner_mutex);
    
    return 0;
}

/*
=== EXPLANATION OF KEY TECHNOLOGIES USED IN THIS PROGRAM ===

1. SOCKET PROGRAMMING:
   - socket(): Creates network endpoints for communication
   - bind(): Associates a socket with a specific port number
   - listen(): Makes the socket ready to accept incoming connections
   - Used here to: Create network servers on different ports (8080-8084)
   - Purpose: Each port can serve files to network clients

2. OPENDIR/READDIR (Directory Operations):
   - opendir(): Opens a directory for reading its contents
   - readdir(): Reads each file/folder entry in the directory
   - closedir(): Closes the directory after reading
   - Used here to: Scan folders (files/1, files/2, etc.) and list available files
   - Purpose: Discover what files are available for sharing on each port

3. POSIX THREADS (pthread):
   - pthread_create(): Creates new threads that run concurrently
   - pthread_mutex_lock/unlock(): Prevents data corruption when multiple threads access shared data
   - pthread_join(): Waits for threads to finish before program exits
   - Used here to: Allow simultaneous port scanning and file operations
   - Purpose: Enable multiple ports to operate independently and safely

PROGRAM FLOW:
1. Creates a thread that sequentially scans ports 8080-8084
2. Binds to the first available port using socket programming
3. Uses opendir/readdir to scan the corresponding folder for files
4. Displays a menu where users can list available files
5. All operations are thread-safe using POSIX mutex locks
*/



