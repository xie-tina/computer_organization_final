#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>

using namespace std;

// Define a struct for instructions
struct Instruction {
    string op;       // Operation: add, sub, lw, sw, beq
    int rd = 0, rs = 0, rt = 0;  // Registers for R-type
    int imm = 0;     // Immediate value for I-type
};

// Define CPU components
vector<int> registers(32, 1); // 32 registers initialized to 1
vector<int> memory(32, 1);    // Memory with 32 words initialized to 1

// Pipeline stages
struct PipelineStage {
    Instruction instr;
    bool active = false;
    string signals;
    int aluResult = 0; // Result of ALU operation
};
PipelineStage IF, ID, EX, MEM, WB;

// Read instructions from a file
vector<Instruction> readInstructions(const string& filename) {
    ifstream infile(filename);
    vector<Instruction> instructions;
    string line;
    while (getline(infile, line)) {
        Instruction instr;
        stringstream ss(line);
        ss >> instr.op;
        if (instr.op == "lw" || instr.op == "sw") {
            char reg;
            ss >> reg >> instr.rt >> instr.imm >> reg >> instr.rs;
        } else if (instr.op == "add" || instr.op == "sub") {
            char reg;
            ss >> reg >> instr.rd >> reg >> instr.rs >> reg >> instr.rt;
        } else if (instr.op == "beq") {
            char reg;
            ss >> reg >> instr.rs >> reg >> instr.rt >> instr.imm;
        }
        instructions.push_back(instr);
    }
    return instructions;
}

// Execute ALU operation
int executeALU(const string& op, int operand1, int operand2) {
    if (op == "add") return operand1 + operand2;
    if (op == "sub") return operand1 - operand2;
    if (op == "lw" || op == "sw") return operand1 + operand2; // Address calculation
    return 0; // Default case
}

// Simulate the pipeline for one cycle
void simulateCycle(vector<Instruction>& instructions, vector<string>& output, int& cycleCount) {
    cycleCount++;
    stringstream cycleOutput;
    cycleOutput << "Cycle: " << cycleCount << endl;

    // WB Stage
    if (WB.active) {
        if (WB.instr.op == "add" || WB.instr.op == "sub" || WB.instr.op == "lw") {
            registers[WB.instr.rd] = WB.aluResult;
        }
        WB.active = false;
    }
    cycleOutput << "WB: " << (WB.active ? WB.instr.op : "NOP") << endl;

    // MEM Stage
    if (MEM.active) {
        if (MEM.instr.op == "lw") {
            MEM.aluResult = memory[MEM.aluResult / 4]; // Load word
        } else if (MEM.instr.op == "sw") {
            memory[MEM.aluResult / 4] = registers[MEM.instr.rt]; // Store word
        }
        WB = MEM;
        MEM.active = false;
    }
    cycleOutput << "MEM: " << (MEM.active ? MEM.instr.op : "NOP") << endl;

    // EX Stage
    if (EX.active) {
        int operand1 = registers[EX.instr.rs];
        int operand2 = (EX.instr.op == "lw" || EX.instr.op == "sw") ? EX.instr.imm : registers[EX.instr.rt];
        EX.aluResult = executeALU(EX.instr.op, operand1, operand2);
        MEM = EX;
        EX.active = false;
    }
    cycleOutput << "EX: " << (EX.active ? EX.instr.op : "NOP") << endl;

    // ID Stage
    if (ID.active) {
        EX = ID;
        ID.active = false;
    }
    cycleOutput << "ID: " << (ID.active ? ID.instr.op : "NOP") << endl;

    // IF Stage
    if (!instructions.empty()) {
        IF.instr = instructions.front();
        instructions.erase(instructions.begin());
        ID = IF;
        IF.active = false;
    }
    cycleOutput << "IF: " << (IF.active ? IF.instr.op : "NOP") << endl;

    cycleOutput << "-----------------------------------\\n";
    output.push_back(cycleOutput.str());
}

// Write output to file
void writeOutputToFile(const string& filename, const vector<string>& output) {
    ofstream outfile(filename);
    for (const auto& line : output) {
        outfile << line;
    }
}

int main() {
    string inputFile = "../input/test3.txt"; // Input file
    string outputFile = "../output/output3.txt"; // Output file

    vector<Instruction> instructions = readInstructions(inputFile);
    vector<string> output;

    int cycleCount = 0;
    while (!instructions.empty() || WB.active || MEM.active || EX.active || ID.active || IF.active) {
        simulateCycle(instructions, output, cycleCount);
    }

    // Append final states to output
    stringstream finalOutput;
    finalOutput << "Simulation completed in " << cycleCount << " cycles.\\n";
    finalOutput << "Final Register Values:\\n";
    for (int i = 0; i < 32; i++) {
        finalOutput << "$" << i << ": " << registers[i] << " ";
        if ((i + 1) % 8 == 0) finalOutput << "\\n";
    }
    finalOutput << "Final Memory Values:\\n";
    for (int i = 0; i < 32; i++) {
        finalOutput << "M[" << i << "]: " << memory[i] << " ";
        if ((i + 1) % 8 == 0) finalOutput << "\\n";
    }
    output.push_back(finalOutput.str());

    writeOutputToFile(outputFile, output);

    cout << "Simulation complete. Output written to " << outputFile << endl;
    return 0;
}

