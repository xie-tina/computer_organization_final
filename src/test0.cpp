#include <iostream>
#include <vector>
#include <string>
#include <queue>

using namespace std;

// Constants
const int NUM_REGISTERS = 32;
const int MEMORY_SIZE = 32;

// Data Structures
struct Instruction {
    string op;      // Operation (e.g., lw, sw, add, sub, beq)
    int rd, rs, rt; // Registers
    int immediate;  // Immediate value for lw/sw/beq
    int address;    // Memory address for lw/sw
};

struct CPU {
    vector<int> registers;
    vector<int> memory;
    vector<Instruction> instructions;
    
    // 這裡把 pipeline 想成 5 個 queue，分別對應 IF, ID, EX, MEM, WB
    // 不過在較完整的寫法中，常會用單一指令暫存器 (pipeline register)
    // 而不是一個 queue。但此處先沿用你的程式結構。
    queue<Instruction> pipeline[5]; // IF, ID, EX, MEM, WB
    
    int pc;    // Program counter
    int cycle; // Cycle count
    
    // 為了方便示範 forwarding，需要多存一點資訊 (EX, MEM指令)，
    // 但這裡先簡化：我們直接看 pipeline[2] 的 front() 與 pipeline[3] 的 front()。
    
    // 紀錄每個 clock cycle 產生的輸出 (debug/顯示用)
    vector<string> clockOutput;

    CPU() {
        registers.resize(NUM_REGISTERS, 1);
        registers[0] = 0;  // $0 is always 0
        memory.resize(MEMORY_SIZE, 1);
        pc = 0;
        cycle = 0;
    }

    void loadInstructions(const vector<Instruction>& insts) {
        instructions = insts;
    }

    void simulate();
    void fetch();
    void decode();
    void execute();
    void memoryAccess();
    void writeBack();
    void advancePipeline();
    void printPipeline();
    void printFinalResults();
};

// Helper function to print control signals
string controlSignals(const string& op) {
    if (op == "lw")  return "RegDst=0 ALUSrc=1 Branch=0 MemRead=1 MemWrite=0 RegWrite=1 MemToReg=1";
    if (op == "sw")  return "RegDst=X ALUSrc=1 Branch=0 MemRead=0 MemWrite=1 RegWrite=0 MemToReg=X";
    if (op == "add") return "RegDst=1 ALUSrc=0 Branch=0 MemRead=0 MemWrite=0 RegWrite=1 MemToReg=0";
    if (op == "sub") return "RegDst=1 ALUSrc=0 Branch=0 MemRead=0 MemWrite=0 RegWrite=1 MemToReg=0";
    if (op == "beq") return "RegDst=X ALUSrc=0 Branch=1 MemRead=0 MemWrite=0 RegWrite=0 MemToReg=X";
    return "RegDst=X ALUSrc=X Branch=X MemRead=X MemWrite=X RegWrite=X MemToReg=X";
}

// Fetch Instruction (IF)
void CPU::fetch() {
    // 先檢查 PC 是否超過指令數量
    if (pc < (int)instructions.size()) {
        Instruction inst = instructions[pc];
        pipeline[0].push(inst);           // 放到 IF stage (queue[0])
        clockOutput.push_back(inst.op + " IF");
        pc++; // 指向下一個指令 (Predict Not Taken: 先抓下一條)
    }
}

// Decode Instruction (ID) + Hazard Detection
void CPU::decode() {
    if (pipeline[0].empty()) return;

    Instruction inst = pipeline[0].front();
    pipeline[0].pop();

    bool needsStall = false;

    // ------------------------------------------------------------
    // Forwarding / Hazard Detection:
    // 檢查 EX 階段 (pipeline[2]) 與 MEM 階段 (pipeline[3]) 的前一、前二指令
    // ------------------------------------------------------------
    // 1) 如果前一指令(在 EX) 或前二指令(在 MEM) 有生產出 rd，且該 rd == 當前指令的 rs or rt，
    //    就需要 forwarding。
    // 2) 如果前一指令是 lw，且它還在 EX 階段，則無法 forward (資料還沒從 MEM 拿到)，
    //    此時必須 stall。
    // ------------------------------------------------------------

    // 檢查 EX 階段
    if (!pipeline[2].empty()) {
        Instruction prevEX = pipeline[2].front();
        
        // 前一指令是否會寫回 rd？(add, sub, lw 都會寫到 rd，sw, beq 不會)
        bool prevEXWritesRegister =
            (prevEX.op == "add" || prevEX.op == "sub" || prevEX.op == "lw");

        // 如果會寫回且 rd == 當前指令的 rs 或 rt
        if (prevEXWritesRegister && prevEX.rd != 0) {
            if (prevEX.rd == inst.rs || prevEX.rd == inst.rt) {
                // 如果前一指令是 lw，且還在 EX，表示還沒拿到記憶體資料，無法 forward
                // => stall
                if (prevEX.op == "lw") {
                    needsStall = true;
                } else {
                    // 其它 (add, sub) 可以 EX -> EX forwarding
                    // 這裡只有印訊息，實際要 forward 的話，
                    // 一般要在 EX 階段把暫存器值帶過去。
                    cout << "[Forwarding from EX] " << inst.op << " depends on " 
                         << prevEX.op << " (rd=" << prevEX.rd << ")\n";
                }
            }
        }
    }

    // 檢查 MEM 階段
    if (!pipeline[3].empty()) {
        Instruction prevMEM = pipeline[3].front();
        
        // 前二指令是否會寫回 rd？
        bool prevMEMWritesRegister =
            (prevMEM.op == "add" || prevMEM.op == "sub" || prevMEM.op == "lw");
        
        if (prevMEMWritesRegister && prevMEM.rd != 0) {
            if (prevMEM.rd == inst.rs || prevMEM.rd == inst.rt) {
                // lw 已經讀完資料(在 MEM 階段讀了，所以可以 forward)，
                // add/sub 也在 EX 就算出結果了 (MEM 階段也能 forward)。
                cout << "[Forwarding from MEM] " << inst.op << " depends on " 
                     << prevMEM.op << " (rd=" << prevMEM.rd << ")\n";
            }
        }
    }

    // 若需要 stall，則把指令塞回 IF stage，並回復 pc (不再往下推)
    if (needsStall) {
        pipeline[0].push(inst); // 把指令丟回 IF，不往下送
        // pc-- 的設計見仁見智，因為你已經 pc++ 了，
        // 但是實體硬體多半是 pipeline bubble 的概念。
        // 這裡為了程式模擬，暫時不動 pc 也可以，
        // 因為我們只是“重複”把同一指令擋在 ID。
        cout << "Cycle " << cycle << ": Data hazard detected, stalling pipeline\n";
        return;
    }

    // 通過 hazard check 就可以送到 ID queue
    pipeline[1].push(inst);
    clockOutput.push_back(inst.op + " ID");
}

// Execute Instruction (EX)
void CPU::execute() {
    if (pipeline[1].empty()) return;

    Instruction inst = pipeline[1].front();
    pipeline[1].pop();

    // 如果是 beq，在此階段判斷是否要 branch
    if (inst.op == "beq") {
        int rsValue = registers[inst.rs];
        int rtValue = registers[inst.rt];

        // Predict Not Taken: 如果真正要跳，則需要 flush pipeline 的 IF 階段
        // (一般來說，ID 也要 flush，但此處簡化示範)
        if (rsValue == rtValue) {
            // 實際硬體上會計算新 PC，這邊直接加上 immediate
            pc += inst.immediate; 

            // flush IF stage
            while (!pipeline[0].empty()) {
                pipeline[0].pop();
            }

            cout << "[Branch taken] Flushing IF, PC set to " << pc << "\n";
        } else {
            cout << "[Branch not taken] Continue\n";
        }
    }
    else if (inst.op == "add") {
        registers[inst.rd] = registers[inst.rs] + registers[inst.rt];
    }
    else if (inst.op == "sub") {
        registers[inst.rd] = registers[inst.rs] - registers[inst.rt];
    }
    // lw, sw 不在此處做 ALU 計算的話，你也可以把 address 計算放這裡
    // (例如 lw 的 effective address = registers[inst.rs] + inst.immediate)
    // 但此示範直接用 inst.address 代表已經算好的位置。

    pipeline[2].push(inst);
    clockOutput.push_back(inst.op + " EX " + controlSignals(inst.op));
}

// Memory Access (MEM)
void CPU::memoryAccess() {
    if (pipeline[2].empty()) return;

    Instruction inst = pipeline[2].front();
    pipeline[2].pop();

    if (inst.op == "lw") {
        // 從 memory address / 4 位置讀取
        registers[inst.rd] = memory[inst.address / 4];
    }
    else if (inst.op == "sw") {
        // 將 register[rs] 寫入 memory address / 4
        memory[inst.address / 4] = registers[inst.rs];
    }

    pipeline[3].push(inst);
    clockOutput.push_back(inst.op + " MEM " + controlSignals(inst.op));
}

// Write Back (WB)
void CPU::writeBack() {
    if (pipeline[3].empty()) return;

    Instruction inst = pipeline[3].front();
    pipeline[3].pop();

    // add, sub, lw 的寫回動作其實在這裡做或在 MEM 都行(真實硬體因為stage定義不同)。
    // 此處先簡化，已在 EX/lw/MEM 做了結果更新，所以這裡只有印訊息。
    clockOutput.push_back(inst.op + " WB " + controlSignals(inst.op));
}

// Pipeline 前進一個 cycle
void CPU::advancePipeline() {
    clockOutput.clear();

    // 順序必須 WB -> MEM -> EX -> ID -> IF
    // 才能確保下一 cycle 取到正確資訊
    writeBack();
    memoryAccess();
    execute();
    decode();
    fetch();

    cout << "Clock Cycle " << ++cycle << ":\n";
    for (const auto& out : clockOutput) {
        cout << out << endl;
    }
    cout << endl;
}

// Print Final Results
void CPU::printFinalResults() {
    cout << "## Final Result:\n";
    cout << "Total Cycles: " << cycle << endl;

    cout << "Final Register Values:\n";
    for (int i = 0; i < NUM_REGISTERS; i++) {
        cout << registers[i] << " ";
    }
    cout << endl;

    cout << "Final Memory Values:\n";
    for (int i = 0; i < MEMORY_SIZE; i++) {
        cout << memory[i] << " ";
    }
    cout << endl;
}

// Simulate CPU Execution
void CPU::simulate() {
    // 只要 pipeline 還有指令沒走完，都要繼續跑
    while ( pc < (int)instructions.size() ||
            !pipeline[0].empty() ||
            !pipeline[1].empty() ||
            !pipeline[2].empty() ||
            !pipeline[3].empty() )
    {
        advancePipeline();
    }
    printFinalResults();
}

int main() {
    CPU cpu;

    // 指令範例 (可依需求自行修改)
    vector<Instruction> instructions = {
        // lw $2, 8($0)  => 從 memory[8/4 = 2] 讀到 $2
        {"lw", 2, 0, -1,  8},  
        // lw $3, 16($0) => 從 memory[16/4 = 4] 讀到 $3
        {"lw", 3, 0, -1, 16},
        // add $6, $4, $5 => $4 = $2 + $3
        {"add",6, 4, 5, -1},  
        // sw $6, 24($0)  => 將 $4 寫到 memory[24/4 = 6]
        {"sw", 6, 0, -1, 24},
        /*
          
        // beq $2, $3, offset => 若 $2 == $3 則跳 offset
        // 這裡舉例 offset = 1 (往後兩條指令)
        {"beq", -1, 2, 3, 1},
        // add $5, $2, $2
        {"add", 5, 2, 2, -1},
        // sub $6, $3, $2
        {"sub", 6, 3, 2, -1},*/
    };

    cpu.loadInstructions(instructions);
    cpu.simulate();

    return 0;
}
