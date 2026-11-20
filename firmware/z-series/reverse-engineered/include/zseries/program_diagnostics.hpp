// Recovered Program/ProgramParams/ProgramStats information output.
#pragma once
#include "zseries/console.hpp"
#include "zseries/program_repository.hpp"
namespace zseries { namespace oem {
bool report_program_instruction(const ConsoleServices&,const Instruction&);
bool report_program_record(const ConsoleServices&,const ProgramRecord&);
bool report_program_parameters(const ConsoleServices&,const ProgramParameters&);
bool report_program_stats(const ConsoleServices&,const ProgramStats&);
bool report_program_buffer(const ConsoleServices&,const ProgramBuffer&);
bool report_program_estimate(const ConsoleServices&,const ProgramRecord&,double heating_rate,std::uint32_t seconds);
}} // namespace zseries::oem
