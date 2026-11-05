#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
// 64-bit horizontal microinstruction field positions (shared with the CU).
constexpr int W_STALL=63, W_ALU=58, W_SRCA=55, W_SRCB=53, W_ISWB=52, W_ISSCR=51,
              W_SCRADDR=47, W_ISLD=46, W_ISST=45, W_ISCALL=44, W_ISRET=43,
              W_ISUB=42, W_ISBEQ=41, W_ISBGT=40, W_ISMOVZ=39, W_MOVZDST=36,
              W_ISRETZ=35, W_DST=30, W_SRC1=25, W_SRC2=20, W_IMM=4;

const char* kAlu[] = {"ADD","SUB","MUL","DIV","AND","OR","LSL","LSR","ASR",
                      "NOT","CMP","FADD","FSUB","FMUL","FDIV","FCMP","PASS",
                      "MOD","XOR","ITOF","FTOI"};
int aluIndex(const std::string& s){ for(int i=0;i<21;i++) if(s==kAlu[i]) return i;
    throw std::runtime_error("unknown alu '"+s+"'"); }
int srcAIndex(const std::string& s){
    if(s=="op1")return 0; if(s=="const")return 1; if(s=="scratch")return 2;
    if(s=="op2")return 3; if(s=="imm")return 4;
    throw std::runtime_error("unknown srcA '"+s+"'"); }
int srcBIndex(const std::string& s){
    if(s=="op2")return 0; if(s=="imm")return 1; if(s=="const")return 2;
    if(s=="scratch")return 3; throw std::runtime_error("unknown srcB '"+s+"'"); }
uint32_t regField(const std::string& s){
    if(s=="rd"||s=="rs1"||s=="rs2") return 0;
    if(s.size()>=2 && s[0]=='s'){ int n=std::stoi(s.substr(1)); if(n<0||n>15)
        throw std::runtime_error("scratch range"); return 0x10u|(uint32_t)n; }
    throw std::runtime_error("bad register '"+s+"'"); }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: microasm6 <in.masm> [-o <dir>]\n"; return 1; }
    std::string in=argv[1], out=".";
    if (argc==4 && std::string(argv[2])=="-o") out=argv[3];
    if (!out.empty() && out.back()!='/') out.push_back('/');
    try {
        std::ifstream f(in); if(!f) throw std::runtime_error("cannot open "+in);
        std::vector<uint64_t> prog;
        std::unordered_map<std::string,uint32_t> labels;
        std::string line; bool seen=false; int lineno=0;
        while (std::getline(f,line)) {
            ++lineno;
            auto h=line.find('#'); if(h!=std::string::npos) line.erase(h);
            std::istringstream ss(line); std::vector<std::string> tok; std::string t;
            while (ss>>t) tok.push_back(t);
            if (tok.empty()) continue;
            if (tok[0][0]=='.') {
                std::string name=tok[0].substr(1);
                if(!name.empty()&&name.back()==':') name.pop_back();
                if(name.empty()) throw std::runtime_error("empty label line "+std::to_string(lineno));
                if(labels.count(name)) throw std::runtime_error("duplicate label ."+name);
                if(!prog.empty()) prog.back() &= ~(1ULL<<W_STALL);
                labels[name]=(uint32_t)prog.size(); seen=true; continue;
            }
            if(!seen) throw std::runtime_error("instruction outside label line "+std::to_string(lineno));
            uint64_t w=1ULL<<W_STALL;
            int alu=-1, srcA=-1, srcB=0; uint32_t dst=0, src1=0, src2=0; int16_t imm=0;
            bool isWb=false,isScr=false; int scrAddr=0;
            bool ld=false,st=false,call=false,ret=false,ub=false,beq=false,bgt=false,movz=false,retz=false;
            for (size_t i=0;i<tok.size();++i) {
                const std::string& s=tok[i]; auto eq=s.find('=');
                if(eq==std::string::npos) {
                    if(s=="ld")ld=true; else if(s=="st")st=true; else if(s=="call")call=true;
                    else if(s=="ret")ret=true; else if(s=="ub")ub=true; else if(s=="beq")beq=true;
                    else if(s=="bgt")bgt=true; else if(s=="movz")movz=true; else if(s=="retz")retz=true;
                    else if(s=="wb")isWb=true; else if(s=="scratchWb")isScr=true;
                    else throw std::runtime_error("unknown flag '"+s+"'");
                    continue;
                }
                std::string k=s.substr(0,eq), v=s.substr(eq+1);
                if(k=="alu") alu=aluIndex(v);
                else if(k=="srcA") srcA=srcAIndex(v);
                else if(k=="srcB") srcB=srcBIndex(v);
                else if(k=="src1") src1=regField(v);
                else if(k=="src2") src2=regField(v);
                else if(k=="dst") { dst=regField(v); if(v=="rd") isWb=true; else { isScr=true; scrAddr=(int)(dst&0xF);} }
                else if(k=="imm") imm=(int16_t)std::stoi(v,nullptr,0);
                else throw std::runtime_error("unknown field '"+k+"'");
            }
            if(alu<0) throw std::runtime_error("missing alu= line "+std::to_string(lineno));
            if(srcA<0) srcA = (src1 & 0x10u) ? 2 : 0;
            w |= (uint64_t)(alu&0x1F)<<W_ALU;
            w |= (uint64_t)(srcA&0x7)<<W_SRCA;
            w |= (uint64_t)(srcB&0x3)<<W_SRCB;
            if(isWb) w|=1ULL<<W_ISWB;
            if(isScr){ w|=1ULL<<W_ISSCR; w|=(uint64_t)(scrAddr&0xF)<<W_SCRADDR; }
            if(ld)w|=1ULL<<W_ISLD; if(st)w|=1ULL<<W_ISST; if(call)w|=1ULL<<W_ISCALL;
            if(ret)w|=1ULL<<W_ISRET; if(ub)w|=1ULL<<W_ISUB; if(beq)w|=1ULL<<W_ISBEQ;
            if(bgt)w|=1ULL<<W_ISBGT; if(movz)w|=1ULL<<W_ISMOVZ; if(retz)w|=1ULL<<W_ISRETZ;
            w |= (uint64_t)(dst&0x1F)<<W_DST;
            w |= (uint64_t)(src1&0x1F)<<W_SRC1;
            w |= (uint64_t)(src2&0x1F)<<W_SRC2;
            w |= (uint64_t)((uint16_t)imm)<<W_IMM;
            prog.push_back(w);
        }
        if(!prog.empty()) prog.back() &= ~(1ULL<<W_STALL);
        static const char* macros[] = {"nop","add","sub","mul","div","mod","and","or","xor","lsl","lsr","asr","not","mov","cmp","addi","subi","muli","divi","modi","andi","ori","xori","lsli","lsri","asri","movi","cmpi","ld","st","fadd","fsub","fmul","fdiv","fcmp","b","beq","bgt","call","ret","movz","retz","ebreak","ecall","xtime"};
        std::vector<uint8_t> table;
        for (int i=0;i<45;i++){ auto it=labels.find(macros[i]);
            if(it==labels.end()) throw std::runtime_error(std::string("missing .")+macros[i]);
            table.push_back((uint8_t)it->second); }
        std::ofstream rom(out+"microprograms.rom", std::ios::binary);
        for(uint64_t w: prog) for(int b=0;b<8;b++) rom.put((char)((w>>(8*b))&0xFF));
        std::ofstream tab(out+"rom_address_table.rom", std::ios::binary);
        for(uint8_t b: table) tab.put((char)b);
        std::cout << "microasm6: " << prog.size() << " words, 45-byte table\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << "\n"; return 1; }
}
