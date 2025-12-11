/*
 * STUDENT GRADE MANAGEMENT SYSTEM - C BACKEND
 * Complete server implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif

#define PORT 8080
#define BUFFER_SIZE 4096

typedef struct {
    char name[100];
    char branch[100];
    int rollNumber;
    int semester;
    int year;
    float marks[5];
    float total;
    float average;
    char grade[10];
    int gradePoint;
} Student;

// Function prototypes
void calculateGrade(Student *student);
char* determineGradeLetter(float avg);
int determineGradePoint(float avg);
void saveToFile(Student *student);
char* getAllRecords();
void handleRequest(int client_socket, char *request);
void sendResponse(int client_socket, char *status, char *content_type, char *body);
char* urlDecode(char *str);
void parseFormData(char *data, Student *student);
void deleteRecordByRoll(int roll);

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Failed to initialize Winsock\n");
        return 1;
    }
#endif

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(1);
    }

    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(1);
    }

    printf("========================================\n");
    printf("  Student Grade Management Server\n");
    printf("========================================\n");
    printf("Server running on http://localhost:%d\n", PORT);
    printf("Press Ctrl+C to stop the server\n");
    printf("Waiting for connections...\n\n");

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        memset(buffer, 0, BUFFER_SIZE);
        recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        handleRequest(client_socket, buffer);
        close(client_socket);
    }

    close(server_socket);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

void handleRequest(int client_socket, char *request) {
    char method[10], path[256];
    sscanf(request, "%s %s", method, path);

    printf("Request: %s %s\n", method, path);

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        FILE *file = fopen("index.html", "r");
        if (file) {
            fseek(file, 0, SEEK_END);
            long size = ftell(file);
            fseek(file, 0, SEEK_SET);
            char *html = malloc(size + 1);
            fread(html, 1, size, file);
            html[size] = '\0';
            fclose(file);
            sendResponse(client_socket, "200 OK", "text/html", html);
            free(html);
        } else {
            sendResponse(client_socket, "404 Not Found", "text/plain", "HTML file not found");
        }
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/calculate") == 0) {
        char *body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;
            Student student;
            memset(&student, 0, sizeof(student));
            parseFormData(body, &student);
            
            calculateGrade(&student);
            saveToFile(&student);

            char response[1024];
            snprintf(response, sizeof(response),
                "{\"success\":true,\"name\":\"%s\",\"rollNumber\":%d,\"branch\":\"%s\",\"semester\":%d,\"year\":%d,"
                "\"total\":%.2f,\"average\":%.2f,\"grade\":\"%s\",\"gradePoint\":%d}",
                student.name, student.rollNumber, student.branch, student.semester, student.year,
                student.total, student.average, student.grade, student.gradePoint);

            sendResponse(client_socket, "200 OK", "application/json", response);
            printf("Processed: %s (Roll: %d) - Branch: %s - Sem: %d - Year: %d - Grade: %s\n\n",
                   student.name, student.rollNumber, student.branch, student.semester, student.year, student.grade);
        }
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/delete") == 0) {
        char *body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;
            // expect roll=<number>
            
            int roll = 0;
            char *eq = strchr(body, '=');
            if (eq) roll = atoi(eq + 1);
            deleteRecordByRoll(roll);
            sendResponse(client_socket, "200 OK", "application/json", "{\"success\":true}");
            printf("Deleted records with roll: %d\n", roll);
        } else {
            sendResponse(client_socket, "400 Bad Request", "application/json", "{\"success\":false}");
        }
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/records") == 0) {
        char *records = getAllRecords();
        sendResponse(client_socket, "200 OK", "application/json", records);
        free(records);
        return;
    }

    sendResponse(client_socket, "404 Not Found", "text/plain", "Not Found");
}

void sendResponse(int client_socket, char *status, char *content_type, char *body) {
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, strlen(body));

    send(client_socket, header, strlen(header), 0);
    send(client_socket, body, strlen(body), 0);
}

void parseFormData(char *data, Student *student) {
    // safer parsing: split on '&', then split key/value on '='
    int markIndex = 0;
    char *p = data;
    while (p && *p) {
        char *amp = strchr(p, '&');
        char pair[256] = {0};
        if (amp) {
            size_t len = amp - p;
            if (len > sizeof(pair)-1) len = sizeof(pair)-1;
            strncpy(pair, p, len);
            pair[len] = '\0';
            p = amp + 1;
        } else {
            strncpy(pair, p, sizeof(pair)-1);
            p = NULL;
        }

        char *eq = strchr(pair, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = pair;
        char *value = eq + 1;
        char *decoded = urlDecode(value);

        if (strcmp(key, "name") == 0) {
            strncpy(student->name, decoded, sizeof(student->name) - 1);
        } else if (strcmp(key, "branch") == 0) {
            strncpy(student->branch, decoded, sizeof(student->branch) - 1);
        } else if (strcmp(key, "semester") == 0) {
            student->semester = atoi(decoded);
        } else if (strcmp(key, "year") == 0) {
            student->year = atoi(decoded);
        } else if (strcmp(key, "roll") == 0) {
            student->rollNumber = atoi(decoded);
        } else if (strncmp(key, "mark", 4) == 0 && markIndex < 5) {
            student->marks[markIndex++] = atof(decoded);
        }
        free(decoded);
    }
}

char* urlDecode(char *str) {
    char *decoded = malloc(strlen(str) + 1);
    char *pstr = str, *pdecoded = decoded;

    while (*pstr) {
        if (*pstr == '%') {
            if (pstr[1] && pstr[2]) {
                int value;
                sscanf(pstr + 1, "%2x", &value);
                *pdecoded++ = value;
                pstr += 3;
            }
        } else if (*pstr == '+') {
            *pdecoded++ = ' ';
            pstr++;
        } else {
            *pdecoded++ = *pstr++;
        }
    }
    *pdecoded = '\0';
    return decoded;
}

void calculateGrade(Student *student) {
    student->total = 0;
    for (int i = 0; i < 5; i++) {
        student->total += student->marks[i];
    }
    student->average = student->total / 5.0;
    strcpy(student->grade, determineGradeLetter(student->average));
    student->gradePoint = determineGradePoint(student->average);
}

char* determineGradeLetter(float avg) {
    if (avg >= 91 && avg <= 100) return "O";
    else if (avg >= 81 && avg < 91) return "A+";
    else if (avg >= 71 && avg < 81) return "A";
    else if (avg >= 61 && avg < 71) return "B+";
    else if (avg >= 51 && avg < 61) return "B";
    else if (avg >= 40 && avg < 51) return "C";
    else return "D";
}

int determineGradePoint(float avg) {
    if (avg >= 91 && avg <= 100) return 10;
    else if (avg >= 81 && avg < 91) return 9;
    else if (avg >= 71 && avg < 81) return 8;
    else if (avg >= 61 && avg < 71) return 7;
    else if (avg >= 51 && avg < 61) return 6;
    else if (avg >= 40 && avg < 51) return 5;
    else return 0;
}

void saveToFile(Student *student) {
    FILE *file = fopen("grades.txt", "a");
    if (file == NULL) return;

    fprintf(file, "===================================================\n");
    fprintf(file, "Student Name    : %s\n", student->name);
    fprintf(file, "Roll Number     : %d\n", student->rollNumber);
    fprintf(file, "Branch          : %s\n", student->branch);
    fprintf(file, "Semester        : %d\n", student->semester);
    fprintf(file, "Year            : %d\n", student->year);
    fprintf(file, "---------------------------------------------------\n");
    fprintf(file, "Subject Marks:\n");
    for (int i = 0; i < 5; i++) {
        fprintf(file, "  Subject %d     : %.2f\n", i + 1, student->marks[i]);
    }
    fprintf(file, "---------------------------------------------------\n");
    fprintf(file, "Total Marks     : %.2f / 500\n", student->total);
    fprintf(file, "Average         : %.2f%%\n", student->average);
    fprintf(file, "Grade Letter    : %s\n", student->grade);
    fprintf(file, "Grade Point     : %d\n", student->gradePoint);
    fprintf(file, "Status          : %s\n", student->gradePoint == 0 ? "FAIL" : "PASS");
    fprintf(file, "===================================================\n\n");
    fclose(file);
}

char* getAllRecords() {
    FILE *file = fopen("grades.txt", "r");
    if (!file) {
        return strdup("{\"records\":[]}");
    }

    char *json = malloc(100000);
    int pos = 0;
    
    pos += snprintf(json + pos, 100000 - pos, "{\"records\":[");

    char line[512];
    int first = 1;
    char name[100] = {0};
    int roll = 0;
    char branch[100] = {0};
    int semester = 0;
    int year = 0;
    float avg = 0;
    char grade[10] = {0};

    while (fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        if (strstr(line, "Student Name") != NULL) {
            char *colon = strchr(line, ':');
            if (colon) sscanf(colon + 1, " %99[^\n]", name);
        } 
        else if (strstr(line, "Roll Number") != NULL) {
            char *colon = strchr(line, ':');
            if (colon) sscanf(colon + 1, " %d", &roll);
        } else if (strstr(line, "Branch") != NULL) {
            char *colon = strchr(line, ':');
            if (colon) sscanf(colon + 1, " %99[^\n]", branch);
        } else if (strstr(line, "Semester") != NULL) {
            char *colon = strchr(line, ':');
            if (colon) sscanf(colon + 1, " %d", &semester);
        } else if (strstr(line, "Year") != NULL) {
            char *colon = strchr(line, ':');
            if (colon) sscanf(colon + 1, " %d", &year);
        } 
        else if (strstr(line, "Average") != NULL && strstr(line, "Grade") == NULL) {
            char *colon = strchr(line, ':');
            if (colon) {
                char temp[50] = {0};
                sscanf(colon + 1, " %49[^%%]", temp);
                avg = atof(temp);
            }
        } 
        else if (strstr(line, "Grade Letter") != NULL) {
            char *colon = strchr(line, ':');
            if (colon) sscanf(colon + 1, " %9s", grade);
            
            if (!first) pos += snprintf(json + pos, 100000 - pos, ",");
            first = 0;
            
            pos += snprintf(json + pos, 100000 - pos,
                "{\"name\":\"%s\",\"roll\":%d,\"branch\":\"%s\",\"semester\":%d,\"year\":%d,\"average\":%.2f,\"grade\":\"%s\"}",
                name, roll, branch, semester, year, avg, grade);
            
            memset(name, 0, sizeof(name));
            memset(grade, 0, sizeof(grade));
            memset(branch, 0, sizeof(branch));
            roll = 0;
            avg = 0;
            semester = 0;
            year = 0;
        }
    }

    pos += snprintf(json + pos, 100000 - pos, "]}");
    fclose(file);
    return json;
}

void deleteRecordByRoll(int roll) {
    if (roll <= 0) return;
    FILE *file = fopen("grades.txt", "r");
    if (!file) return;
    FILE *tmp = fopen("grades.tmp", "w");
    if (!tmp) { fclose(file); return; }

    char line[512];
    int skip = 0;
    // we'll collect blocks separated by the ===== line
    // if a block contains the target roll, skip writing it
    // strategy: read line by line, buffer block into memory until separator, then decide
    char block[65536];
    size_t bpos = 0;
    block[0] = '\0';

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "===================================================", 51) == 0) {
            // separator encountered; decide on previous block
            if (bpos > 0) {
                block[bpos] = '\0';
                // check if block contains roll
                char search[64];
                snprintf(search, sizeof(search), "Roll Number     : %d", roll);
                if (strstr(block, search) == NULL) {
                    // write previous block
                    fputs(block, tmp);
                }
            }
            // start new block with separator line
            bpos = 0;
            size_t l = strlen(line);
            if (bpos + l < sizeof(block)) {
                memcpy(block + bpos, line, l);
                bpos += l;
            }
        } else {
            size_t l = strlen(line);
            if (bpos + l < sizeof(block)) {
                memcpy(block + bpos, line, l);
                bpos += l;
            }
        }
    }
    // after loop, write last block if not matching
    if (bpos > 0) {
        block[bpos] = '\0';
        char search[64];
        snprintf(search, sizeof(search), "Roll Number     : %d", roll);
        if (strstr(block, search) == NULL) {
            fputs(block, tmp);
        }
    }

    fclose(file);
    fclose(tmp);
    // replace file
    remove("grades.bak");
    rename("grades.txt", "grades.bak");
    rename("grades.tmp", "grades.txt");
}