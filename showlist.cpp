#include <stdio.h>      // Include standard input/output functions (printf, scanf)
#include <dirent.h>     // Include directory entry functions (opendir, readdir, closedir)
#include <stdlib.h>     // Include standard library functions (system)
#include <string.h>     // Include string manipulation functions (strcmp)

// Constants - Define values that won't change during program execution
const char* DIRECTORY_PATH = "/mnt/d/C++/port1";  // Path to the directory to scan
const int MAX_CHOICE = 4;                         // Maximum valid menu choice
const int EXIT_CHOICE = 4;                        // Menu choice to exit program
const int LIST_FILES_CHOICE = 1;                  // Menu choice to list files

// Function declarations - Tell compiler these functions exist before they're used
void showMenu();           // Function to display the menu
void listAvailableFiles(); // Function to scan and display files
void downloadFile();       // Function to handle file downloads (placeholder)
void downloadStatus();     // Function to show download status (placeholder)
void clearScreen();        // Function to clear console screen (commented out)

/**
 * Displays the main application menu
 * This function prints the menu options to the console
 */
void showMenu() {
    printf("\n");                                    // Print empty line for spacing
    printf("Seed App\n");                           // Print application title
    printf("[1] List available files.\n");          // Print menu option 1
    printf("[2] Download file.\n");                 // Print menu option 2
    printf("[3] Download status.\n");               // Print menu option 3
    printf("[%d] Exit.\n", EXIT_CHOICE);           // Print menu option 4 using constant
    printf("\n");                                   // Print empty line for spacing
}

/**
 * Lists all files in the specified directory
 * Uses opendir and readdir to scan directory contents
 * This function opens a directory, reads all files, and displays them numbered
 */
void listAvailableFiles() {
    struct dirent *de;  // Pointer to store directory entry information
    DIR *dr;            // Pointer to directory stream
    int fileCount = 0;  // Counter to track number of files found
    
    // Open the specified directory - using WSL path
    dr = opendir(DIRECTORY_PATH);  // Try to open directory, returns NULL if fails
    
    if (dr == NULL) {  // Check if directory opening failed
        printf("Could not open directory %s\n", DIRECTORY_PATH);  // Print error message
        system("pwd");  // Show current working directory for debugging
        printf("\n");   // Print empty line
        return;         // Exit function early if directory can't be opened
    }
    
    printf("Files Available\n");  // Print header for file list
    
    // Read directory entries and display file names
    // readdir returns NULL when no more entries to read
    while ((de = readdir(dr)) != NULL) {
        // Skip . and .. directory entries (current and parent directory)
        if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
            printf("[%d] %s\n", ++fileCount, de->d_name);  // Print numbered file name
        }
    }
    
    if (fileCount == 0) {  // Check if no files were found
        printf("No files found in directory.\n");  // Print message if directory is empty
    }
    
    closedir(dr);  // Close the directory stream to free system resources
}

/**
 * Placeholder function for file download functionality
 * Will be implemented to use wb and rb file handling modes
 * This function currently just shows a message and waits for user input
 */
void downloadFile() {
    printf("\nDownload file feature not implemented yet.\n");  // Print placeholder message
    getchar();  // Clear input buffer (consume any leftover newline)
    getchar();  // Wait for user to press Enter key
}

/**
 * Placeholder function for download status
 * This function currently just shows a message and waits for user input
 */
void downloadStatus() {
    printf("\nDownload status feature not implemented yet.\n");  // Print placeholder message
    printf("This function will show progress and status of file transfers.\n");  // Explain future functionality
    getchar();  // Clear input buffer (consume any leftover newline)
    getchar();  // Wait for user to press Enter key
}

/**
 * Clears the console screen for better user experience
 * Currently commented out but can be uncommented later
 */
// void clearScreen() {
//     system("clear"); // Linux/WSL command to clear screen
// }

/**
 * Main function - Entry point of the program
 * This function controls the overall program flow and user interaction
 */
int main(void)
{
    int choice;      // Variable to store user's menu selection
    int running = 1; // Flag to control program loop (1 = continue, 0 = exit)
    
    while(running) {  // Main program loop - continues until running becomes 0
        // clearScreen();  // Commented out - would clear screen if uncommented
        
        // Show menu and get user choice
        do {  // Do-while loop to handle menu interaction
            showMenu();           // Display the menu options
            printf("? ");         // Print prompt for user input
            scanf("%d", &choice); // Read user's choice and store in choice variable
            
            switch(choice) {  // Check which menu option was selected
                case LIST_FILES_CHOICE:  // If user chose option 1
                    printf("\nSearching for files...\n");  // Print status message
                    listAvailableFiles();                   // Call function to list files
                    break;                                  // Exit switch statement
                    
                case 2:  // If user chose option 2
                    downloadFile();  // Call download function (placeholder)
                    break;           // Exit switch statement
                    
                case 3:  // If user chose option 3
                    downloadStatus();  // Call status function (placeholder)
                    break;             // Exit switch statement
                    
                case EXIT_CHOICE:  // If user chose option 4 (exit)
                    printf("Exiting...\n");  // Print exit message
                    running = 0;             // Set flag to stop program loop
                    break;                   // Exit switch statement
                    
                default:  // If user entered invalid choice
                    printf("Invalid choice. Please try again.\n");  // Print error message
                    printf("Press Enter to continue...");          // Prompt user to continue
                    getchar();  // Clear input buffer (consume any leftover newline)
                    getchar();  // Wait for user to press Enter key
            }
        } while(choice != EXIT_CHOICE && choice != LIST_FILES_CHOICE);  // Continue loop unless user exits or lists files
    }
    
    return 0;  // Return 0 to indicate successful program completion
}