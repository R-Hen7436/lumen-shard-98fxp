#include <stdio.h>      // Include standard input/output functions (printf, fflush)
#include <stdlib.h>     // Include standard library functions (fopen, fclose, fread, fwrite)
#include <time.h>       // Include time functions (not used in current version but kept for future)

// Constants for file transfer
const int BUFFER_SIZE = 4096;        // 4KB buffer size for reading/writing files in chunks
const char* SOURCE_FILE = "/mnt/d/C++/PORT1/image.png";  // WSL path to source file to copy
const char* DEST_FILE = "/mnt/d/C++/PORT1/download.png"; // WSL path where copied file will be saved
const int PROGRESS_CHUNKS = 5;       // Number of progress updates (file divided into 5 equal parts)

/**
 * Converts bytes to human-readable format (B, KB, MB)
 * This function takes a byte count and displays it in the most appropriate unit
 * 
 * @param bytes The number of bytes to convert and display
 */
void showFileSize(long long bytes) {
    if (bytes < 1024) {  // If less than 1 KB, show in bytes
        printf("%lld bytes", bytes);  // Print exact byte count
    } else if (bytes < 1024 * 1024) {  // If less than 1 MB, show in KB
        printf("%.1f KB", (double)bytes / 1024);  // Convert to KB with 1 decimal place
    } else {  // If 1 MB or larger, show in MB
        printf("%.1f MB", (double)bytes / (1024 * 1024));  // Convert to MB with 1 decimal place
    }
}

/**
 * FUNCTION SUMMARY:
 * This function converts raw byte counts into human-readable sizes.
 * It automatically chooses the best unit (bytes, KB, or MB) based on the file size.
 * For example: 1536 bytes becomes "1.5 KB", 2097152 bytes becomes "2.0 MB".
 * This makes file sizes much easier to read and understand.
 */

/**
 * Shows download progress in "downloading current/total" format
 * This function displays the current progress at each milestone
 * 
 * @param current The number of bytes downloaded so far
 * @param total The total file size in bytes
 */
void showDownloadProgress(long long current, long long total) {
    printf("downloading ");  // Print the start of progress message
    showFileSize(current);   // Call function to show current size in readable format
    printf("/");             // Print separator between current and total
    showFileSize(total);     // Call function to show total size in readable format
    printf("\n");            // Add newline for clean output
    fflush(stdout);          // Force output to display immediately (ensures progress is shown)
}

/**
 * FUNCTION SUMMARY:
 * This function displays the download progress in a clean, readable format.
 * It shows "downloading current/total" where both sizes are converted to human-readable units.
 * The fflush(stdout) ensures that progress messages appear immediately on screen.
 * This gives users real-time feedback on how much of the file has been transferred.
 */

/**
 * Main function - Simple binary file transfer with progress tracking
 * This function orchestrates the entire file transfer process:
 * 1. Opens source and destination files
 * 2. Transfers data in 4KB chunks
 * 3. Shows progress at 5 equal milestones
 * 4. Closes files and confirms completion
 */
int main() {
    FILE *sourceFile, *destFile;     // File pointers for source and destination files
    unsigned char buffer[BUFFER_SIZE]; // Buffer array to hold data chunks during transfer
    size_t bytesRead;                 // Variable to store how many bytes were read in each chunk
    long long totalBytes = 0;         // Total file size in bytes
    long long downloadedBytes = 0;    // Counter for bytes transferred so far
    long long chunkSize = 0;          // Size of each progress milestone (total/5)
    int currentChunk = 0;             // Current milestone number (0-4, representing 1/5 to 5/5)
    
    printf("Simple Binary File Transfer \n");  // Print program title
    
    // Open source file for reading in binary mode (rb)
    sourceFile = fopen(SOURCE_FILE, "rb");  // Open source file in read-binary mode
    if (sourceFile == NULL) {  // Check if file opening failed
        return 1;  // Exit with error code if source file cannot be opened
    }
    
    // Get file size by seeking to end and checking position
    fseek(sourceFile, 0, SEEK_END);  // Move file pointer to end of file
    totalBytes = ftell(sourceFile);   // Get current position (which is the file size)
    fseek(sourceFile, 0, SEEK_SET);  // Move file pointer back to beginning for reading
    
    // Calculate chunk size (total divided by 5 for progress milestones)
    chunkSize = totalBytes / PROGRESS_CHUNKS;  // Divide total size by 5 to get milestone size
    
    // Open destination file for writing in binary mode (wb)
    destFile = fopen(DEST_FILE, "wb");  // Create/open destination file in write-binary mode
    if (destFile == NULL) {  // Check if destination file creation failed
        fclose(sourceFile);   // Close source file before exiting
        return 1;             // Exit with error code if destination file cannot be created
    }
    
    // Transfer loop with progress tracking in 5 chunks
    while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, sourceFile)) > 0) {  // Read chunk from source file
        // Write chunk to destination file
        if (fwrite(buffer, 1, bytesRead, destFile) != bytesRead) {  // Check if write was successful
            printf("\nError: Write failed during transfer\n");  // Print error message if write failed
            break;  // Exit transfer loop if write error occurs
        }
        
        downloadedBytes += bytesRead;  // Add bytes read to total downloaded counter
        
        // Show progress at each 1/5 milestone
        while (currentChunk < PROGRESS_CHUNKS && downloadedBytes >= (currentChunk + 1) * chunkSize) {  // Check if milestone reached
            currentChunk++;  // Move to next milestone
            showDownloadProgress(downloadedBytes, totalBytes);  // Display progress at this milestone
        }
    }
    
    // Show final progress if not already shown (handles case where file size is exact multiple of chunk size)
    if (downloadedBytes == totalBytes && currentChunk < PROGRESS_CHUNKS) {  // Check if final milestone needs to be shown
        showDownloadProgress(downloadedBytes, totalBytes);  // Show final progress message
    }
    
    // Close files to free system resources and ensure data is written
    fclose(sourceFile);  // Close source file
    fclose(destFile);    // Close destination file
    
    printf("\n\n Transfer Complete \n");  // Print completion message
    
    return 0;  // Return 0 to indicate successful completion
}

/**
 * FUNCTION SUMMARY:
 * This is the main function that controls the entire file transfer process.
 * It implements a chunked file transfer system that reads a source file in 4KB blocks
 * and writes them to a destination file, all while tracking progress at 5 equal milestones.
 * 
 * Key features:
 * - Binary mode transfer (rb/wb) for accurate file copying
 * - 4KB buffer for efficient memory usage
 * - Progress tracking at 20%, 40%, 60%, 80%, and 100% completion
 * - Error handling for file operations
 * - Automatic file size detection
 * 
 * The function ensures that files are transferred accurately and efficiently,
 * providing user feedback at regular intervals during the transfer process.
 */
