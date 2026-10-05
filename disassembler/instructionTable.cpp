#include "disassembler.hpp"

const std::unordered_map<instructionName, instructionDetails> Disassembler::instructionTable = {
    {"nop",  {instructionOpcode(0x00), instructionFields(0)}},
    {"add",  {instructionOpcode(0x01), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"sub",  {instructionOpcode(0x02), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"mul",  {instructionOpcode(0x03), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"div",  {instructionOpcode(0x04), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"and",  {instructionOpcode(0x05), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"or",   {instructionOpcode(0x06), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"lsl",  {instructionOpcode(0x07), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"lsr",  {instructionOpcode(0x08), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"asr",  {instructionOpcode(0x09), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"not",  {instructionOpcode(0x0A), instructionFields(HAS_RD | HAS_RS1)}},
    {"mov",  {instructionOpcode(0x0B), instructionFields(HAS_RD | HAS_RS2)}},
    {"cmp",  {instructionOpcode(0x0C), instructionFields(HAS_RS1 | HAS_RS2)}},

    {"addi", {instructionOpcode(0x0D), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"subi", {instructionOpcode(0x0E), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"muli", {instructionOpcode(0x0F), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"divi", {instructionOpcode(0x10), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"andi", {instructionOpcode(0x11), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"ori",  {instructionOpcode(0x12), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"lsli", {instructionOpcode(0x13), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"lsri", {instructionOpcode(0x14), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"asri", {instructionOpcode(0x15), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"movi", {instructionOpcode(0x16), instructionFields(HAS_RD | HAS_IMM)}},
    {"cmpi", {instructionOpcode(0x17), instructionFields(HAS_RS1 | HAS_IMM)}},
    {"ld",   {instructionOpcode(0x18), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},
    {"st",   {instructionOpcode(0x19), instructionFields(HAS_RD | HAS_RS1 | HAS_IMM)}},

    {"fadd", {instructionOpcode(0x1A), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"fsub", {instructionOpcode(0x1B), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"fmul", {instructionOpcode(0x1C), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"fdiv", {instructionOpcode(0x1D), instructionFields(HAS_RD | HAS_RS1 | HAS_RS2)}},
    {"fcmp", {instructionOpcode(0x1E), instructionFields(HAS_RS1 | HAS_RS2)}},

    {"b",    {instructionOpcode(0x1F), instructionFields(HAS_OFFSET)}},
    {"beq",  {instructionOpcode(0x20), instructionFields(HAS_OFFSET)}},
    {"bgt",  {instructionOpcode(0x21), instructionFields(HAS_OFFSET)}},
    {"call", {instructionOpcode(0x22), instructionFields(HAS_OFFSET)}},
    {"ret",  {instructionOpcode(0x23), instructionFields(0)}}
};
