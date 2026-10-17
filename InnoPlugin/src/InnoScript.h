// InnoScript.h

#ifndef ZIP7_INC_INNO_SCRIPT_H
#define ZIP7_INC_INNO_SCRIPT_H

#include "inno/setup/info.hpp"
#include "inno/setup/language.hpp"

#include <string>

namespace NArchive {
namespace NInno {

// Reconstructs an Inno Setup script from the setup headers (incomplete).
void GenerateInstallScript(const setup::info &info, std::string &out);

// Writes the [LangOptions] section of a language (.isl) file.
void GenerateLanguageFile(const setup::language_entry &lang, std::string &out);

}}

#endif
