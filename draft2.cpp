#include <iostream>

#include <vector>

#include <cstring>

#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include <algorithm>

 

const int PORT_START = 8080;

const int PORT_END = 8084;

 

using namespace std;

 

class PortScanner {

private:

    vector<int> availablePorts;

    vector<int> selectedPorts;

 

    bool isPortAvailable(int port) {

        int sock = socket(AF_INET, SOCK_STREAM, 0);

        if (sock < 0) return false;

 

        struct sockaddr_in addr;

        memset(&addr, 0, sizeof(addr));

        addr.sin_family = AF_INET;

        addr.sin_addr.s_addr = INADDR_ANY;

        addr.sin_port = htons(port);

 

        int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));

        close(sock);

 

        return result == 0;

    }

 

public:

    void scanPorts() {

        cout << "Port Results\n";

        cout << "Scanning ports " << PORT_START << "-" << PORT_END << "...\n\n";

 

        for (int port = PORT_START; port <= PORT_END; ++port) {

            if (isPortAvailable(port)) {

                availablePorts.push_back(port);

                cout << "Port " << port << ": AVAILABLE\n";

            } else {

                cout << "Port " << port << ": IN USE\n";

            }

        }

    }

 

    void selectPorts() {

        if (availablePorts.empty()) {

            cout << "No available ports found. Exiting...\n";

            exit(1);

        }

 

        cout << "\n  PORT SELECTION\nAvailable ports: ";

        for (int port : availablePorts) {

            cout << port << " ";

        }

        cout << "\n\nSelect ports to use:\n";

        cout << "1. Use a specific port\n";

        cout << "2. Use all available ports\n";

        cout << "Enter your choice (1-2): ";

 

        int choice;

        cin >> choice;

 

        if (choice == 1) {

            int port;

            cout << "Enter the port number: ";

            cin >> port;

 

            if (find(availablePorts.begin(), availablePorts.end(), port) != availablePorts.end()) {

                selectedPorts.push_back(port);

            } else {

                cout << "Port " << port << " is not available!\n";

                exit(1);

            }

        } else if (choice == 2) {

            selectedPorts = availablePorts;

        } else {

            cout << "Invalid choice!\n";

            exit(1);

        }

    }

 

    void displaySelectedPorts() {

        cout << "\nSELECTED PORTS\nUsing ports: ";

        for (int port : selectedPorts) {

            cout << port << " ";

        }

        cout << "\n\nPort selection complete. You can now use these ports for your application.\n";

    }

};

 

int main() {

    PortScanner scanner;

    scanner.scanPorts();

    scanner.selectPorts();

    scanner.displaySelectedPorts();

    return 0;