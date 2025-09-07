#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <errno.h>
#include <sys/select.h>
#include <locale.h>

// Define CRTSCTS if not available (safer approach)
#ifndef CRTSCTS
    #define CRTSCTS 0
#endif

#define MAX_DEVICES_INITIAL 10
#define MAX_NAME_LEN 64
#define BUFFER_SIZE 256

typedef struct {
    char mac[18];      // XX:XX:XX:XX:XX:XX format
    int rssi;
    char name[MAX_NAME_LEN];
} ble_device_t;

static ble_device_t *devices = NULL;
static int device_count = 0;
static int device_capacity = 0;
static int csv_output = 0; // Flag for CSV output mode

// Function to configure serial port
int configure_serial_port(int fd) {
    struct termios tty;
    
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return -1;
    }

    // Set baud rate to 115200
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    // 8N1 mode
    tty.c_cflag &= ~PARENB;        // No parity
    tty.c_cflag &= ~CSTOPB;        // 1 stop bit
    tty.c_cflag &= ~CSIZE;         // Clear size mask
    tty.c_cflag |= CS8;            // 8 data bits
    tty.c_cflag &= ~CRTSCTS;       // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL; // Enable reading, ignore modem control lines

    // Raw input mode
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // Raw output mode
    tty.c_oflag &= ~OPOST;

    // Set timeout: return after 1 second or when data available
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}

// Function to initialize dynamic array
int init_device_array() {
    device_capacity = MAX_DEVICES_INITIAL;
    devices = malloc(device_capacity * sizeof(ble_device_t));
    if (devices == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for device array\n");
        return -1;
    }
    device_count = 0;
    return 0;
}

// Function to resize device array if needed
int resize_device_array() {
    if (device_count >= device_capacity) {
        int new_capacity = device_capacity * 2;
        ble_device_t *new_devices = realloc(devices, new_capacity * sizeof(ble_device_t));
        if (new_devices == NULL) {
            fprintf(stderr, "Error: Failed to resize device array\n");
            return -1;
        }
        devices = new_devices;
        device_capacity = new_capacity;
    }
    return 0;
}

// Function to cleanup dynamic array
void cleanup_device_array() {
    if (devices != NULL) {
        free(devices);
        devices = NULL;
    }
    device_count = 0;
    device_capacity = 0;
}

// Function to read a line from serial port with timeout
int read_line_with_timeout(int fd, char *buffer, int buffer_size, int timeout_seconds) {
    fd_set readfds;
    struct timeval timeout;
    int pos = 0;
    char c;
    
    time_t start_time = time(NULL);
    
    while (pos < buffer_size - 1) {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int result = select(fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (result < 0) {
            perror("select");
            return -1;
        } else if (result == 0) {
            // Timeout - check total elapsed time
            if (time(NULL) - start_time >= timeout_seconds) {
                return -2; // Total timeout exceeded
            }
            continue;
        }
        
        if (read(fd, &c, 1) != 1) {
            continue;
        }
        
        if (c == '\n') {
            buffer[pos] = '\0';
            return pos;
        } else if (c != '\r') {
            buffer[pos++] = c;
        }
    }
    
    buffer[pos] = '\0';
    return pos;
}

// Function to parse device line: DEV,MAC,RSSI,NAME
int parse_device_line(const char *line, ble_device_t *device) {
    if (strncmp(line, "DEV,", 4) != 0) {
        return 0; // Not a device line
    }
    
    // Create a working copy of the line
    char line_copy[256];
    strncpy(line_copy, line + 4, sizeof(line_copy) - 1); // Skip "DEV,"
    line_copy[sizeof(line_copy) - 1] = '\0';
    
    char *token;
    int field = 0;
    
    device->name[0] = '\0'; // Initialize empty name
    
    token = strtok(line_copy, ",");
    while (token != NULL && field < 3) {
        switch (field) {
            case 0: // MAC address
                strncpy(device->mac, token, sizeof(device->mac) - 1);
                device->mac[sizeof(device->mac) - 1] = '\0';
                break;
            case 1: // RSSI
                device->rssi = atoi(token);
                break;
            case 2: // Name (may be empty)
                strncpy(device->name, token, sizeof(device->name) - 1);
                device->name[sizeof(device->name) - 1] = '\0';
                break;
        }
        field++;
        token = strtok(NULL, ",");
    }
    
    return (field >= 2); // At least MAC and RSSI must be present
}

// Comparison function for qsort (sort by RSSI descending)
int compare_devices(const void *a, const void *b) {
    const ble_device_t *dev_a = (const ble_device_t *)a;
    const ble_device_t *dev_b = (const ble_device_t *)b;
    return dev_b->rssi - dev_a->rssi; // Descending order
}

// Function to check if MAC already exists and update if better RSSI
int add_or_update_device(const ble_device_t *new_device) {
    // Look for existing device with same MAC
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].mac, new_device->mac) == 0) {
            // Device already exists - update if new RSSI is better (higher)
            if (new_device->rssi > devices[i].rssi) {
                devices[i].rssi = new_device->rssi;
                // Update name if the new one is not empty and current is empty
                if (new_device->name[0] != '\0' && devices[i].name[0] == '\0') {
                    strncpy(devices[i].name, new_device->name, sizeof(devices[i].name) - 1);
                    devices[i].name[sizeof(devices[i].name) - 1] = '\0';
                }
            }
            return 0; // Device updated, no new device added
        }
    }
    
    // Device not found, resize array if needed and add new device
    if (resize_device_array() != 0) {
        return 0; // Failed to resize array
    }
    
    devices[device_count] = *new_device;
    device_count++;
    return 1; // New device added
}

void print_usage(const char *program_name) {
    printf("Usage: %s [--csv] <serial_port> <timeout_ms>\n", program_name);
    printf("Options:\n");
    printf("  --csv    Output results in CSV format\n");
    printf("Examples:\n");
    printf("  %s /dev/ttyUSB0 10000\n", program_name);
    printf("  %s --csv /dev/tty.usbserial-XXXX 15000\n", program_name);
}

int main(int argc, char *argv[]) {
    // Configure UTF-8 locale for proper character handling
    if (setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
        // Fallback to system default locale
        setlocale(LC_CTYPE, "");
    }
    
    // Parse command line arguments
    int arg_index = 1;
    const char *port;
    int timeout_ms;
    
    // Check for --csv option
    if (argc >= 2 && strcmp(argv[1], "--csv") == 0) {
        csv_output = 1;
        arg_index = 2;
    }
    
    // Check remaining arguments
    if (argc - arg_index != 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    port = argv[arg_index];
    timeout_ms = atoi(argv[arg_index + 1]);
    
    if (timeout_ms < 1000 || timeout_ms > 300000) {
        fprintf(stderr, "Error: Timeout must be between 1000ms and 300000ms\n");
        return 1;
    }

    // Initialize dynamic device array
    if (init_device_array() != 0) {
        return 1;
    }

    // Open serial port
    int fd = open(port, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open serial port %s: %s\n", port, strerror(errno));
        cleanup_device_array();
        return 1;
    }

    // Configure serial port
    if (configure_serial_port(fd) != 0) {
        fprintf(stderr, "Error: Failed to configure serial port\n");
        close(fd);
        cleanup_device_array();
        return 1;
    }

    printf("BLE Scanner Client\n");
    printf("Port: %s, Timeout: %dms\n", port, timeout_ms);
    printf("Sending scan command...\n");

    // Send SCAN command
    char command[64];
    snprintf(command, sizeof(command), "SCAN %d\n", timeout_ms);  // Use LF only as requested
    size_t cmd_len = strlen(command);
    if (write(fd, command, cmd_len) != (ssize_t)cmd_len) {
        fprintf(stderr, "Error: Failed to send command\n");
        close(fd);
        cleanup_device_array();
        return 1;
    }

    printf("Scanning for BLE devices...\n");

    // Read responses
    char buffer[BUFFER_SIZE];
    device_count = 0;
    int scan_timeout = (timeout_ms / 1000) + 10; // Add 10 seconds buffer
    
    while (1) {
        int result = read_line_with_timeout(fd, buffer, sizeof(buffer), scan_timeout);
        
        if (result < 0) {
            if (result == -2) {
                fprintf(stderr, "Error: Timeout waiting for scan completion\n");
            } else {
                fprintf(stderr, "Error reading from serial port\n");
            }
            close(fd);
            cleanup_device_array();
            return 1;
        }
        
        if (result == 0) {
            continue; // Empty line, keep reading
        }
        
        // Check for end marker
        if (strcmp(buffer, "END") == 0) {
            break;
        }
        
        // Parse device line
        ble_device_t device;
        if (parse_device_line(buffer, &device)) {
            add_or_update_device(&device);
        }
    }

    close(fd);

    // Sort devices by RSSI (descending)
    if (device_count > 0) {
        qsort(devices, device_count, sizeof(ble_device_t), compare_devices);
    }

    // Print results
    if (csv_output) {
        // CSV output format
        printf("MAC,RSSI,Name\n");
        for (int i = 0; i < device_count; i++) {
            printf("%s,%d,%s\n", 
                   devices[i].mac, 
                   devices[i].rssi,
                   devices[i].name[0] ? devices[i].name : "");
        }
    } else {
        // Table output format
        printf("\nScan completed. Found %d device(s):\n", device_count);
        
        if (device_count == 0) {
            printf("No BLE devices found.\n");
        } else {
            printf("%-17s %6s %-20s\n", "MAC Address", "RSSI", "Device Name");
            printf("%-17s %6s %-20s\n", "-----------", "----", "-----------");
            
            for (int i = 0; i < device_count; i++) {
                printf("%-17s %4d   %s\n", 
                       devices[i].mac, 
                       devices[i].rssi,
                       devices[i].name[0] ? devices[i].name : "<No Name>");
            }
        }
    }

    // Cleanup
    cleanup_device_array();
    return 0;
}
