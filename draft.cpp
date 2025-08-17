#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_PORTS 5
#define PORT_START 8080
#define PORT_END 8084

// Function to check if a port is available
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

// Function to scan available ports
void scan_ports(int available_ports[], int *count) {
    *count = 0;
   
    printf("=== PORT SCANNER ===\n");
    printf("Scanning ports %d-%d...\n\n", PORT_START, PORT_END);
   
    for (int port = PORT_START; port <= PORT_END; port++) {
        if (is_port_available(port)) {
            available_ports[(*count)++] = port;
            printf("Port %d: AVAILABLE\n", port);
        } else {
            printf("Port %d: IN USE\n", port);
        }
    }
}

int main() {
    int available_ports[MAX_PORTS];
    int port_count = 0;
    int selected_ports[MAX_PORTS];
    int selected_count = 0;
   
    // Scan for available ports
    scan_ports(available_ports, &port_count);
   
    if (port_count == 0) {
        printf("No available ports found. Exiting...\n");
        return 1;
    }
   
    // Display available ports
    printf("\n=== PORT SELECTION ===\n");
    printf("Available ports: ");
    for (int i = 0; i < port_count; i++) {
        printf("%d ", available_ports[i]);
    }
    printf("\n");
   
    // Port selection menu
    printf("\nSelect ports to use:\n");
    printf("1. Use a specific port\n");
    printf("2. Use all available ports\n");
    printf("Enter your choice (1-2): ");
   
    int choice;
    scanf("%d", &choice);
   
    if (choice == 1) {
        int port;
        printf("Enter the port number: ");
        scanf("%d", &port);
       
        int valid = 0;
        for (int i = 0; i < port_count; i++) {
            if (available_ports[i] == port) {
                valid = 1;
                break;
            }
        }
       
        if (valid) {
            selected_ports[0] = port;
            selected_count = 1;
        } else {
            printf("Port %d is not available!\n", port);
            return 1;
        }
    } else if (choice == 2) {
        // Use all available ports
        for (int i = 0; i < port_count; i++) {
            selected_ports[i] = available_ports[i];
        }
        selected_count = port_count;
    } else {
        printf("Invalid choice!\n");
        return 1;
    }
   
    // Display selected ports
    printf("\n=== SELECTED PORTS ===\n");
    printf("Using ports: ");
    for (int i = 0; i < selected_count; i++) {
        printf("%d ", selected_ports[i]);
    }
    printf("\n");
   
    // Here you would continue with the operations for the selected ports
    printf("\nPort selection complete. You can now use these ports for your application.\n");
   
    return 0;
}