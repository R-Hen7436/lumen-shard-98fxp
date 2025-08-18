#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>
#include <arpa/inet.h>

const int MAX_PORTS = 5;
const int PORT_START = 8080;
const int PORT_END = 8084;
const int MAX_FILES = 100;
const int MAX_FILENAME_LENGTH = 256;

// Single port data
typedef struct {
    int port;
    int folder_id;
    char folder_path[256];
    int socket_fd;
    int is_bound;
} port_data_t;

// Global instance data for multiple bound ports
port_data_t bound_ports[MAX_PORTS];
int bound_port_count = 0;
char unique_files[MAX_FILES][MAX_FILENAME_LENGTH];
int unique_file_count = 0;

// Checks if a specific port is available for binding
// Returns 1 if port is free, 0 if port is already in use
// Used by try_bind_port() to test ports before actual binding
int is_port_available(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 0;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);

    return result == 0;
}

// Actually binds to a port and starts listening for connections
// Returns socket file descriptor on success, -1 on failure
// Called by try_bind_port() when a port is confirmed available
int bind_and_listen(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

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

// Adds a filename to the unique files list, avoiding duplicates
// Checks if file already exists before adding to prevent duplicates
// Called by scan_folder_files() when it finds files in folders
void add_unique_file(const char* filename) {
    // Check if file already exists in the list
    for (int i = 0; i < unique_file_count; i++) {
        if (strcmp(unique_files[i], filename) == 0) {
            return; // File already exists, don't add duplicate
        }
    }
    
    // Add new unique file
    if (unique_file_count < MAX_FILES) {
        strcpy(unique_files[unique_file_count], filename);
        unique_file_count++;
    }
}

// Scans a specific folder and adds all files to the unique files list
// Opens a directory, reads all files, and calls add_unique_file() for each
// Called by try_bind_port() after successful binding and by listAvailableFiles()
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
        // Skip . and .. directory entries
        if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
            add_unique_file(de->d_name);
            files_found++;
        }
    }
    
    printf("found %d file(s)\n", files_found);
    closedir(dr);
}

// Attempts to bind to a specific port and set up its folder
// First checks if port is available, then binds and scans its folder
// Called by sequential_port_scan() for each port in the range
int try_bind_port(int port) {
    printf("Trying port %d... ", port);
    
    if (is_port_available(port)) {
        printf("available, attempting to bind... ");
        
        int sock = bind_and_listen(port);
        if (sock >= 0) {
            // Successfully bound - add to our bound ports list
            bound_ports[bound_port_count].port = port;
            bound_ports[bound_port_count].folder_id = port - PORT_START + 1;
            bound_ports[bound_port_count].socket_fd = sock;
            bound_ports[bound_port_count].is_bound = 1;
            snprintf(bound_ports[bound_port_count].folder_path, 
                     sizeof(bound_ports[bound_port_count].folder_path), 
                     "files/%d", bound_ports[bound_port_count].folder_id);
            
            printf("SUCCESS!\n");
            printf("Bound to port %d, assigned folder %s\n", 
                   bound_ports[bound_port_count].port, 
                   bound_ports[bound_port_count].folder_path);
            
            // Scan files from this port's folder
            scan_folder_files(bound_ports[bound_port_count].folder_path);
            
            bound_port_count++;
            return 1; // Success
        } else {
            printf("failed to bind.\n");
        }
    } else {
        printf("in use.\n");
    }
    
    return 0; // Failed to bind
}

// Scans ports 8080-8084 one by one until it finds an available port
// Stops after successfully binding to the first available port
// Called by main() to find and bind to exactly one port per instance
void sequential_port_scan() {
    printf("Starting sequential port scanning...\n");
    
    for (int port = PORT_START; port <= PORT_END; port++) {
        if (try_bind_port(port)) {
            // Successfully bound to a port, stop scanning
            printf("Port binding successful. Stopping scan.\n");
            break;
        }
        
        // Small delay between port attempts
        sleep(1);
    }
    
    printf("\nPort scanning complete.\n");
    printf("Successfully bound to %d port(s).\n", bound_port_count);
}

// Shows all unique files from the bound port's folder (Menu Option 1)
// Rescans the folder to get fresh file list and displays them
// Called by show_menu() when user selects option 1
void listAvailableFiles() {
    printf("Searching for files... ");
    
    // Clear the unique files list and rescan all bound ports
    unique_file_count = 0;
    
    // Rescan files from all bound ports
    for (int i = 0; i < bound_port_count; i++) {
        scan_folder_files(bound_ports[i].folder_path);
    }
    
    printf("done.\n");
    printf("Files available.\n");
    
    if (unique_file_count == 0) {
        printf("No files found in any bound port directories.\n");
    } else {
        for (int i = 0; i < unique_file_count; i++) {
            printf("[%d] %s\n", i + 1, unique_files[i]);
        }
    }
    
    // Show which ports are currently bound
    printf("\nCurrently bound ports: ");
    for (int i = 0; i < bound_port_count; i++) {
        printf("%d ", bound_ports[i].port);
    }
    printf("(Total: %d)\n", bound_port_count);
}

// Displays the main menu and handles user input
// Loops until user chooses option 4 (Exit)
// Called by main() after successful port binding
void show_menu() {
    int choice;
    do {
        printf("\nSeed App Menu (Port %d - Folder %s)\n", 
               bound_ports[0].port, bound_ports[0].folder_path);
        printf("[1] List available files.\n");
        printf("[2] Download file.\n");
        printf("[3] Download status.\n");
        printf("[4] Exit.\n");
        printf("\n?");

        scanf("%d", &choice);

        switch (choice) {
            case 1:
                printf("Listing available files...\n\n");
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

// Main program entry point - coordinates the entire application flow
// 1. Initializes variables, 2. Scans for available port, 3. Shows menu, 4. Cleans up
// This is where the program starts and ends
int main() {
    printf("Starting Seed App...\n");
    printf("Will scan ports %d to %d sequentially (one at a time)...\n", PORT_START, PORT_END);
    
    // Initialize bound ports array
    bound_port_count = 0;
    unique_file_count = 0;
    
    // Perform sequential port scanning and binding
    sequential_port_scan();
    
    if (bound_port_count == 0) {
        printf("\nNo available ports found in range %d-%d. Exiting...\n", 
               PORT_START, PORT_END);
        return 1;
    }
    
    printf("\nThis instance is serving files from %d folder:\n", bound_port_count);
    for (int i = 0; i < bound_port_count; i++) {
        printf("  Port %d → %s\n", bound_ports[i].port, bound_ports[i].folder_path);
    }
    
    printf("\nTotal unique files found: %d\n", unique_file_count);
    
    // Show the menu
    show_menu();
    
    // Clean up all bound sockets
    printf("Cleaning up...\n");
    for (int i = 0; i < bound_port_count; i++) {
        if (bound_ports[i].socket_fd >= 0) {
            close(bound_ports[i].socket_fd);
            printf("Closed socket for port %d\n", bound_ports[i].port);
        }
    }
    
    printf("Goodbye!\n");
    return 0;
}


