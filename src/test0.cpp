#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>

using namespace std;

// Define a struct for instructions
struct Instruction {
    string op;       // Operation: add, sub, lw, sw, beq
    int rd, rs, rt;  // Registers for R-type
    int imm;         // Immediate value for I-type
    int address;     // Address for memory instructions
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

// Forwarding logic placeholders
bool forwardingEnabled = true;
bool stallRequired = false;

// Forwarding check function
bool checkForwarding(const Instruction& currentInstr, const PipelineStage& memStage, const PipelineStage& wbStage) {
    if (forwardingEnabled) {
        if (memStage.active && (currentInstr.rs == memStage.instr.rd || currentInstr.rt == memStage.instr.rd)) {
            return true;
        }
        if (wbStage.active && (currentInstr.rs == wbStage.instr.rd || currentInstr.rt == wbStage.instr.rd)) {
            return true;
        }
    }
    return false;
}

// Execute ALU operation
int executeALU(const string& op, int operand1, int operand2) {
    if (op == "add") return operand1 + operand2;
    if (op == "sub") return operand1 - operand2;
    if (op == "lw" || op == "sw") return operand1 + operand2; // Address calculation
    return 0; // Default case
}

// Read instructions from a file
vector<Instruction> readInstructions(const string& filename) {
    ifstream infile(filename);
    vector<Instruction> instructions;
    string line;
    while (getline(infile, line)) {
        Instruction instr;
        // Parse the line to populate instr (based on instruction format)
        // Example: lw $2, 8($0) => op="lw", rd=2, imm=8, rs=0
        instructions.push_back(instr);
    }
    return instructions;
}

// Write output to a file
void writeOutput(const string& filename, int cycleCount, const vector<int>& registers, const vector<int>& memory) {
    ofstream outfile(filename);
    outfile << "Simulation completed in " << cycleCount << " cycles.\n\n";

    outfile << "Final Register Values:\n";
    for (int i = 0; i < 32; i++) {
        outfile << "$" << i << ": " << registers[i] << " ";
        if ((i + 1) % 8 == 0) outfile << endl;
    }
    outfile << "\nFinal Memory Values:\n";
    for (int i = 0; i < 32; i++) {
        outfile << "M[" << i << "]: " << memory[i] << " ";
        if ((i + 1) % 8 == 0) outfile << endl;
    }
}

// Simulate the pipeline for one cycle
void simulateCycle(vector<Instruction>& instructions, int& cycleCount) {
    cycleCount++;

    // WB Stage
    if (WB.active) {
        // Write back results to registers
        if (WB.instr.op == "add" || WB.instr.op == "sub" || WB.instr.op == "lw") {
            registers[WB.instr.rd] = WB.aluResult;
        }
        WB.active = false;
    }

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

    // EX Stage
    if (EX.active) {
        int operand1 = registers[EX.instr.rs];
        int operand2 = (EX.instr.op == "lw" || EX.instr.op == "sw") ? EX.instr.imm : registers[EX.instr.rt];

        if (checkForwarding(EX.instr, MEM, WB)) {
            // Forwarding logic: get updated values from MEM or WB stage
            if (MEM.active && EX.instr.rs == MEM.instr.rd) operand1 = MEM.aluResult;
            if (WB.active && EX.instr.rs == WB.instr.rd) operand1 = WB.aluResult;

            if (MEM.active && EX.instr.rt == MEM.instr.rd) operand2 = MEM.aluResult;
            if (WB.active && EX.instr.rt == WB.instr.rd) operand2 = WB.aluResult;
        }

        EX.aluResult = executeALU(EX.instr.op, operand1, operand2);
        MEM = EX;
        EX.active = false;
    }

    // ID Stage
    if (ID.active) {
        if (stallRequired) {
            stallRequired = false; // Stall for one cycle
        } else {
            EX = ID;
            ID.active = false;
        }
    }

    // IF Stage
    if (!instructions.empty() && !stallRequired) {
        IF.instr = instructions.front();
        instructions.erase(instructions.begin());
        ID = IF;
        IF.active = false;
    }
}

// Output results for debugging
void outputPipelineState(int cycle) {
    cout << "Cycle: " << cycle << endl;
    cout << "IF: " << (IF.active ? IF.instr.op : "NOP") << endl;
    cout << "ID: " << (ID.active ? ID.instr.op : "NOP") << endl;
    cout << "EX: " << (EX.active ? EX.instr.op : "NOP") << endl;
    cout << "MEM: " << (MEM.active ? MEM.instr.op : "NOP") << endl;
    cout << "WB: " << (WB.active ? WB.instr.op : "NOP") << endl;
    cout << "-----------------------------------\n";
}

int main() {
    string inputFile = "../input/test3.txt";
    string outputFile = "../output/result.txt";

    vector<Instruction> instructions = readInstructions(inputFile);

    int cycleCount = 0;

    while (!instructions.empty() || WB.active || MEM.active || EX.active || ID.active || IF.active) {
        simulateCycle(instructions, cycleCount);
        outputPipelineState(cycleCount);
    }

    writeOutput(outputFile, cycleCount, registers, memory);

    cout << "Simulation completed. Results written to " << outputFile << endl;

    return 0;
}
