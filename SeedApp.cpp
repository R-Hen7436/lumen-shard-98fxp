#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace std;

// Function declarations
void showMenu();
bool isPortAvailable(int port);
void findAvailablePorts();
int selectPort();

void findAvailablePorts() {
    const int MAX_PORTS = 5;
    const int PORT_START = 8080;
    const int PORT_END = 8084;

    int available_ports[MAX_PORTS];
    int count = 0;

    for (int port = PORT_START; port <= PORT_END; port++) {
        if(isPortAvailable(port)) {
            available_ports[count++] = port;
        }
}
    cout << "Found port ";
    for(int i = 0; i < count; i++){
        cout << available_ports[i];
        if(i < count -1) cout << ", ";

    }
    cout << "." << endl << endl;
}

bool isPortAvailable(int port) {
    int test_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(test_socket < 0) {
        return false;
    }

    // Set socket options
    int opt = 1;
    if(setsockopt(test_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        close(test_socket);
        return false;
    }
    
    struct sockaddr_in test_addr;
    test_addr.sin_family = AF_INET;
    test_addr.sin_addr.s_addr = INADDR_ANY;
    test_addr.sin_port = htons(port);
    
    int bind_result = bind(test_socket, (struct sockaddr*)&test_addr, sizeof(test_addr));
    close(test_socket);
    
    return bind_result == 0;
}

int selectPort() {
    int port;
    cout << "? ";
    cin >> port;
    
    // Validate port range
    if(port < 8080 || port > 8084) {
        cout << "Invalid port. Using default port 8080." << endl;
        port = 8080;
    }
    
    return port;
}

void showMenu() {
    cout << endl;
    cout << " Seed App" << endl;
    cout << " [1] List available files." << endl;
    cout << " [2] Download file." << endl;
    cout << " [3] Download status." << endl;
    cout << " [4] Exit." << endl;
    cout << endl;
}

int main() {
    bool running = true;
    
    while(running) {
        cout << "Seed App" << endl;
        cout << "Finding available ports ... ";
        
        // Find and display available ports
        findAvailablePorts();
        
        // Get port selection from user
        int selected_port = selectPort();
        
        cout << "Listening at port " << selected_port << "." << endl << endl;
        
        // Main application loop
        int choice;
        do {
            showMenu();
            cout << "? ";
            cin >> choice;
            
            switch(choice) {
                case 1:
                    cout << "List available files feature not implemented yet." << endl;
                    break;
                case 2:
                    cout << "Download file feature not implemented yet." << endl;
                    break;
                case 3:
                    cout << "Download status feature not implemented yet." << endl;
                    break;
                case 4:
                    cout << "Exiting to port selection..." << endl;
                    break;
                default:
                    cout << "Invalid choice. Please try again." << endl;
            }
        } while(choice != 4);
    }
    
    return 0;
}
