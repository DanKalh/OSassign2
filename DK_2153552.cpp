/*Dan Kalhori 2153552 COSC 3360

code runs with commands:

g++ main.cpp -o test
./test sample_matrix.txt sample_words.txt

*/


#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string>
#include <string.h>
#include <vector>
#include <numeric>
#include <algorithm>

using namespace std;

class Process {
public:
    string id;  
    int absDeadline;
    int timeRemaining;
    vector<string> instructions;
    vector<int> maxDemand;
    vector<int> allocation;
    vector<int> request;
    vector<int> need;
};

int process_n, resource_n;
vector<int> availResource;
vector<Process> processes;
vector<vector<string>> resourceType;
vector<string> resourceNames = {"R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10"};


// Function declarations
void readMatrix(char* inFile);
void readWords(char* inFile);
void scheduleProcess(bool edf);
vector<int> readArgs(string& line);
vector<int> sortIndices(const vector<int> &primary, const vector<int> &secondary);
void printProcess(int processID, int currentTick);
bool isSafe(int processID);
void childProcess(int idx, int toChild, int fromChild);
void initializePipes(int pipes[][4], int processCount);
void handleChildProcesses(int pipes[][4], int processCount);
void performScheduling(int pipes[][4], bool useEDF);
bool checkComplete(const vector<bool>& ended);



int main(int argc, char *argv[]) {
    readMatrix(argv[1]);
    readWords(argv[2]);
    cout << "EDF:" << endl;
    scheduleProcess(true);

    readMatrix(argv[1]);
    cout << "LLF:" << endl;
    scheduleProcess(false);
	
	cout << endl;
}


void readMatrix(char* inFile) {
    string line;
    ifstream file(inFile);
    if (!file) {
        cerr << "Unable to open file: " << inFile << endl;
        exit(1); // Make sure to handle file open errors
    }

    // Read the number of resources
    getline(file, line);
    resource_n = stoi(line);

    // Read the number of processes
    getline(file, line);
    process_n = stoi(line);

    // Read available resources
    getline(file, line);
    istringstream iss(line);
    availResource.resize(resource_n);
    for (int i = 0; i < resource_n; i++) {
        iss >> availResource[i];
    }

    processes.resize(process_n);

    // Read maximum demand for each process
    for (int i = 0; i < process_n; i++) {
        getline(file, line);
        istringstream iss1(line);
        processes[i].maxDemand.resize(resource_n);
        processes[i].need.resize(resource_n, 0);
        for (int j = 0; j < resource_n; j++) {
            iss1 >> processes[i].maxDemand[j];
            processes[i].need[j] = processes[i].maxDemand[j];
        }
        processes[i].allocation.resize(resource_n, 0);
        processes[i].request.resize(resource_n, 0);
    }

    // Read instructions for each process
    for (int i = 0; i < process_n; i++) {
        getline(file, line);
        processes[i].id = line; // Process ID or name

        getline(file, line);
        processes[i].absDeadline = stoi(line); // Deadline

        getline(file, line);
        processes[i].timeRemaining = stoi(line); // Time remaining

        // Instructions parsing
        processes[i].instructions.clear(); // Ensure instructions are initially empty
        while (getline(file, line) && !line.empty()) { // Read until an empty line or end of file
            if (line.back() == ';') line.pop_back(); // Remove trailing semicolon
            processes[i].instructions.push_back(line);
            if (line.find("end.") != string::npos) break; // Stop if 'end.' is found
        }
    }
}

void readWords(char* inFile) {
    ifstream file(inFile);
    resourceType.resize(resource_n);
    string line;
    while (getline(file, line)) {
        istringstream iss(line);
        string token;
        vector<string> tempResources;

        // Split line by ':' to separate resource type from words
        if (getline(iss, token, ':')) {
            // Optionally process token if it's the resource type or identifier
            // Skip spaces until the list of words
            iss >> ws;

            // Now, process the list of words separated by commas
            while (getline(iss, token, ',')) {
                // Trim leading and trailing spaces from each word
                size_t start = token.find_first_not_of(" ");
                size_t end = token.find_last_not_of(" ");
                if (start != string::npos && end != string::npos) {
                    tempResources.push_back(token.substr(start, (end - start + 1)));
                } else if (start != string::npos) { // Case where token contains only one non-space character
                    tempResources.push_back(token.substr(start));
                }
            }
            // After processing all words, add them to the corresponding resourceType entry
            if (!tempResources.empty()) {
                resourceType.push_back(tempResources);
            }
        }
    }
}



void initializePipes(int pipes[][4], int processCount) {
    for (int i = 0; i < processCount; i++) {
        if (pipe(pipes[i]) < 0 || pipe(pipes[i] + 2) < 0) {
            cout << "Error: Pipe initialization failed." << endl;
            exit(EXIT_FAILURE);
        }
    }
}

void handleChildProcesses(int pipes[][4], int processCount) {
    pid_t processID;
    for (int i = 0; i < processCount; i++) {
        processID = fork();
        if (processID == 0) {
            for (int j = 0; j < processCount; j++) {
                if (i != j) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                    close(pipes[j][2]);
                    close(pipes[j][3]);
                }
            }
            close(pipes[i][0]);
            close(pipes[i][3]);
            childProcess(i, pipes[i][2], pipes[i][1]);
            exit(EXIT_SUCCESS);
        } else if (processID < 0) {
            cout << "Error: Failed to fork child process." << endl;
            exit(EXIT_FAILURE);
        } else {
            close(pipes[i][1]);
            close(pipes[i][2]);
        }
    }
}

//check complete for scheudling
bool checkComplete(const vector<bool>& ended) {
    return all_of(ended.begin(), ended.end(), [](bool ended) { return ended; });
}

void performScheduling(int pipes[][4], bool useEDF) {
    vector<bool> processCompleted(process_n, false);
    bool processWaiting[process_n] = {false};
    int activeProcesses = process_n;
    int timeUnit = 0;
    int maximumDeadline = 0;
    for (const auto& proc : processes) {
        maximumDeadline = max(maximumDeadline, proc.absDeadline);
    }

    while (activeProcesses) {
        vector<int> primaryMetrics;
        vector<int> secondaryMetrics;
        for (int i = 0; i < process_n; i++) {
            if (useEDF) {
                primaryMetrics.push_back(processes[i].absDeadline);
                secondaryMetrics.push_back(-processes[i].timeRemaining);
            } else {
                primaryMetrics.push_back(processes[i].absDeadline - processes[i].timeRemaining);
                secondaryMetrics.push_back(processes[i].timeRemaining);
            }
        }

        vector<int> sortedIndices = sortIndices(primaryMetrics, secondaryMetrics);
        for (auto idx : sortedIndices) {
            if (processCompleted[idx]) continue;

            bool hasRequest = false;
            char responseMsg[] = "ok";
            char buffer[256];
            if (!processWaiting[idx]) {
                while (true) {
                    read(pipes[idx][3], buffer, sizeof(buffer));
                    string instruction(buffer);
                    vector<int> parameters = readArgs(instruction);

                    if (instruction.find("calculate") != string::npos || instruction.find("use_resources") != string::npos) {
                        timeUnit += parameters[0];
                        processes[idx].timeRemaining -= parameters[0];
                        write(pipes[idx][2], responseMsg, strlen(responseMsg) + 1);
                        continue;
                    } else if (instruction.find("print_resources_used") != string::npos) {
                        timeUnit++;
                        processes[idx].timeRemaining--;
                        write(pipes[idx][2], responseMsg, strlen(responseMsg) + 1);
                        read(pipes[idx][3], buffer, sizeof(buffer));  // Confirm completion
                        write(pipes[idx][2], responseMsg, strlen(responseMsg) + 1);
                        continue;
                    } else if (instruction.find("release") != string::npos) {
                        write(pipes[idx][2], responseMsg, strlen(responseMsg) + 1);
                        for (int j = 0; j < resource_n; j++) {
                            availResource[j] += parameters[j];
                            processes[idx].allocation[j] -= parameters[j];
                            processes[idx].need[j] += parameters[j];
                            for (int k = 0; k < parameters[j]; k++) {
                                read(pipes[idx][3], buffer, sizeof(buffer));
                                resourceType[j].push_back(string(buffer));
                                write(pipes[idx][2], responseMsg, strlen(responseMsg) + 1);
                            }
                        }
                    } else if (instruction.find("end") != string::npos) {
                        write(pipes[idx][2], responseMsg, strlen(responseMsg) + 1);
                        for (int j = 0; j < resource_n; j++) {
                            availResource[j] += processes[idx].allocation[j];
                            processes[idx].need[j] += processes[idx].allocation[j];
                            for (int k = 0; k < processes[idx].allocation[j]; k++) {
                                read(pipes[idx][3], buffer, sizeof(buffer));
                                resourceType[j].push_back(string(buffer));
                                write(pipes[idx][2], responseMsg, strlen(responseMsg) + 1);
                            }
                            processes[idx].allocation[j] = 0;
                        }
                        processCompleted[idx] = true;
                        activeProcesses--;
                        cout << processes[idx].id << " " << instruction << endl;
                        break;
                    } else if (instruction.find("request") != string::npos) {
                        for (int j = 0; j < resource_n; j++)
                            processes[idx].request[j] = parameters[j];
                        hasRequest = true;
                    }

                    timeUnit++;
                    processes[idx].timeRemaining--;
                    break;
                }
            }
            if (processWaiting[idx] || hasRequest) {
                processWaiting[idx] = false;
                for (int j = 0; j < resource_n; j++) {
                    if (processes[idx].request[j] > processes[idx].need[j]) {
                        exit(EXIT_FAILURE);
                    }
                }
                for (int j = 0; j < resource_n; j++) {
                    if (processes[idx].request[j] > availResource[j]) {
                        processWaiting[idx] = true;
                        break;
                    }
                }
                if (processWaiting[idx]) {
                    if (hasRequest)
                        cout << "Process is waiting..." << endl;
                    continue;
                }
                if (isSafe(idx)) {
                    if (!hasRequest)
                        cout << processes[idx].id << " is requesting resources" << endl;
                    for (int j = 0; j < resource_n; j++) {
                        for (int k = 0; k < processes[idx].request[j]; k++) {
                            string resourceInfo = resourceType[j].back();
                            resourceType[j].pop_back();
                            write(pipes[idx][2], resourceInfo.c_str(), resourceInfo.length() + 1);
                            read(pipes[idx][3], buffer, sizeof(buffer));
                        }
                    }
                    write(pipes[idx][2], responseMsg, strlen(responseMsg) + 1);
                } else {
                    processWaiting[idx] = true;
                    if (hasRequest)
                        cout << "Process continues to wait..." << endl << endl;
                    continue;
                }
            }
            printProcess(idx, timeUnit);
        }
        if (checkComplete(processCompleted) || timeUnit > maximumDeadline) {
            break;
        }
    }
}


void childProcess(int idx, int toChild, int fromChild) {
    vector<int> resources(10, 0);  // Example with 10 types of resources
    char response[256];  // Buffer for reading responses

    for (const auto& instruction : processes[idx].instructions) {
        // Send the instruction to the parent
        write(fromChild, instruction.c_str(), instruction.length() + 1);

        if (instruction.find("calculate") != string::npos || 
            instruction.find("use_resources") != string::npos ||
            instruction.find("release") != string::npos ||
            instruction.find("end") != string::npos) {
            read(toChild, response, sizeof(response));
            // Handle responses for resource allocation and release
            if (instruction.find("use_resources") != string::npos) {
                istringstream iss(response);
                int resIndex, count;
                while (iss >> resIndex >> count) {
                    resources[resIndex] += count;
                }
            } else if (instruction.find("release") != string::npos) {
                istringstream iss(response);
                int resIndex, count;
                while (iss >> resIndex >> count) {
                    resources[resIndex] -= count;
                    if (resources[resIndex] < 0) resources[resIndex] = 0; // Ensure no negative counts
                }
            }
        } else if (instruction.find("print_resources_used") != string::npos) {
            // Print the master string of resources used
			//FIXME	
            cout << processes[idx].id << " -- Master string: ";
            bool first = true;
            for (size_t i = 0; i < resources.size(); i++) {
                if (resources[i] > 0) {
                    if (!first) cout << ", ";
                    cout << resourceNames[i] << ": " << resources[i] << (resources[i] > 1 ? " units" : " unit");
                    first = false;
                }
            }
            cout << endl;
        } else if (instruction.find("request") != string::npos) {
            read(toChild, response, sizeof(response));
        }
    }

    close(fromChild);
    close(toChild);
}



void scheduleProcess(bool useEDF) {
    int pipes[process_n][4];
    initializePipes(pipes, process_n);
    handleChildProcesses(pipes, process_n);
    performScheduling(pipes, useEDF);
}

vector<int> sortIndices(const vector<int> &primary, const vector<int> &secondary) {
    // Initialize original index locations
    vector<int> idx(primary.size());
    iota(idx.begin(), idx.end(), 0);

    // Sort
    sort(idx.begin(), idx.end(),
         [&primary, &secondary](size_t i1, size_t i2) {
              if (primary[i1] < primary[i2])
                  return true;
              else if (primary[i1] == primary[i2] && secondary[i1] < secondary[i2])
                  return true;
              else
                  return false;
          });

    return idx;
}

vector<int> readArgs(string& line) {
    vector<int> args;
    if (line.find("(") == string::npos)
        return args;
    string token;
    istringstream iss(line);
    getline(iss, token, '(');
    getline(iss, token, ')');
    istringstream iss1(token);
    while (getline(iss1, token, ',')) {
        args.push_back(stoi(token));
    }

    return args;
}

bool isSafe(int processID) {
    bool isSafe = true; // Renaming to camelCase for consistency
    int work[resource_n];
    bool finish[process_n] = {false};

    // Initially adjust resources based on the request
    for (int i = 0; i < resource_n; i++) {
        availResource[i] -= processes[processID].request[i];
        processes[processID].allocation[i] += processes[processID].request[i];
        processes[processID].need[i] -= processes[processID].request[i];
        work[i] = availResource[i];
    }

    // Main loop to find a safe sequence
    while (true) {
        int index = -1;
        for (int i = 0; i < process_n; i++) {
            if (finish[i]) continue;

            bool canAllocate = true;
            for (int j = 0; j < resource_n; j++) {
                if (processes[i].need[j] > work[j]) {
                    canAllocate = false;
                    break;
                }
            }

            if (canAllocate) {
                index = i;
                break;
            }
        }

        if (index == -1) break; // No suitable process found, break the loop

        // Allocate resources to the selected process and mark it as finished
        for (int j = 0; j < resource_n; j++) {
            work[j] += processes[index].allocation[j];
        }
        finish[index] = true;
    }

    // Verify if all processes could finish
    for (int i = 0; i < process_n; i++) {
        if (!finish[i]) {
            isSafe = false;
            break;
        }
    }

    // Revert resource adjustments if the state is not safe
    if (!isSafe) {
        for (int i = 0; i < resource_n; i++) {
            availResource[i] += processes[processID].request[i];
            processes[processID].allocation[i] -= processes[processID].request[i];
            processes[processID].need[i] += processes[processID].request[i];
        }
    }

    return isSafe;
}

// Helper function to print resource lists
void printResourceList(const string& label, const vector<int>& resources) {
    cout << setw(20) << left << label << ": ";
    for (int res : resources) {
        cout << setw(3) << res << " ";
    }
    cout << endl;
}

void printProcess(int processID, int currentTick) {
    cout << "Process ID: " << processes[processID].id << endl;
    printResourceList("Available resources", availResource);
    printResourceList("Allocated resources", processes[processID].allocation);
    printResourceList("Needed resources", processes[processID].need);

    int deadline_miss = max(0, currentTick - processes[processID].absDeadline);
    cout << setw(20) << left << "Deadline miss" << ": " << deadline_miss << endl << endl;
}
