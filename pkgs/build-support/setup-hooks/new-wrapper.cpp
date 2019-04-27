
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>
#include <utility>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

constexpr int ErrorExitCode = 1;
constexpr auto ErrorPrefix = "nix-program-wrapper: ";
constexpr auto MakeErrorPrefix = "nix-program-wrapper --make-wrapper: ";

bool getLineFields(std::string const &line,
    std::vector<std::string> &fields, std::string &error)
{
    fields.clear();

    std::size_t const lineLen = line.size();

    if (lineLen == 0 || line[0] == '#') {
        return true;
    }

    std::string current;

    for (std::size_t pos = 0; pos < lineLen; pos++) {
        char ch = line[pos];

        if (ch == ' ') {
            fields.push_back(std::move(current));
            current.clear();
            continue;
        }

        if (ch == '\\') {
            if (pos + 1 >= lineLen) {
                error = "Truncated \\ escape sequence.";
                return false;
            }

            ch = line[pos + 1];
            pos++;
        }

        current.push_back(ch);
    }

    fields.push_back(std::move(current));

    return true;
}

std::vector<std::string> splitListValue(std::string const &value, char separator)
{
    std::vector<std::string> result;

    if (value.empty()) {
        return result;
    }

    std::string current;

    for (char const ch : value) {
        if (ch == separator) {
            result.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }

    result.push_back(std::move(current));

    return result;
}

std::string concatListValue(std::vector<std::string> const &elems, char separator)
{
    std::string value;
    bool first = true;

    for (std::string const &elem : elems) {
        if (!first) {
            value.push_back(separator);
        }
        first = false;
        value.append(elem);
    }

    return value;
}

bool vectorContainsString(std::vector<std::string> const &vec, std::string const &str)
{
    return std::find(vec.begin(), vec.end(), str) != vec.end();
}

std::string updateListVar(std::string const &currentVal,
    std::string const &extraVal, char separator, bool prefix)
{
    // Get lists of current end extra elements.
    std::vector<std::string> currentElems = splitListValue(currentVal, separator);
    std::vector<std::string> extraElems = splitListValue(extraVal, separator);

    // Remove from current those elements that are in extra.
    for (auto it = currentElems.begin(); it != currentElems.end(); ) {
        if (vectorContainsString(extraElems, *it)) {
            it = currentElems.erase(it);
        } else {
            ++it;
        }
    }

    // Merge current and extra.
    std::vector<std::string> mergedElems;
    if (prefix) {
        mergedElems = std::move(extraElems);
        mergedElems.insert(mergedElems.end(), currentElems.begin(), currentElems.end());
    } else {
        mergedElems = std::move(currentElems);
        mergedElems.insert(mergedElems.end(), extraElems.begin(), extraElems.end());
    }

    // Concatenate the elements and return.
    return concatListValue(mergedElems, separator);
}

std::string escapeString(std::string const &str)
{
    std::string result;
    for (char const ch : str) {
        if (ch == ' ' || ch == '\n' || ch == '\\') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    return result;
}

enum class Argv0Mode {Wrapper, Wrapped, Custom};

int runWrapper(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << ErrorPrefix << "Too few arguments passed.\n";
        return ErrorExitCode;
    }

    // Argument 1 is the path to the wrapper, that is the file that called
    // us via #! and which contains wrapping instructions.
    std::string const inputFile = argv[1];

    Argv0Mode argv0Mode = Argv0Mode::Wrapped;
    std::string argv0Custom;

    bool execFound = false;
    std::string execPath;

    std::vector<std::string> argsBefore;

    {
        std::ifstream fileStream(inputFile);
        if (!fileStream.is_open()) {
            auto const err = errno;
            std::cerr << ErrorPrefix << inputFile <<
                ": Failed to open file: " << strerror(err) << "\n";
            return ErrorExitCode;
        }

        std::size_t lineNum = 1;
        std::string line;
        std::vector<std::string> fields;
        
        for (; std::getline(fileStream, line); lineNum++) {
            auto const printLineError = [&](auto const &errText) {
                std::cerr << ErrorPrefix << inputFile <<
                    ": line " << lineNum << ": " << errText << "\n";
            };

            std::string fieldsError;
            if (!getLineFields(line, fields, fieldsError)) {
                printLineError(fieldsError);
                return ErrorExitCode;
            }
            
            if (fields.size() == 0) {
                continue;
            }

            std::string const &cmd = fields[0];

            auto const printInvalidNumParamsError = [&]() {
                std::ostringstream ss;
                ss << "Invalid number of parameters to \"" << cmd << "\" command.";
                printLineError(ss.str());
            };

            if (cmd == "set" || cmd == "set-default") {
                if (fields.size() != 3) {
                    printInvalidNumParamsError();
                    return ErrorExitCode;
                }

                std::string const &varName = fields[1];
                std::string const &value = fields[2];
                bool const overwrite = (cmd == "set");

                auto const res = setenv(varName.c_str(), value.c_str(), overwrite);
                if (res != 0) {
                    auto const err = errno;
                    std::ostringstream ss;
                    ss << "Failed to set environment variable \"" <<
                        varName << "\": " << strerror(err);
                    printLineError(ss.str());
                    return ErrorExitCode;
                }
            }
            else if (cmd == "unset") {
                if (fields.size() != 2) {
                    printInvalidNumParamsError();
                    return ErrorExitCode;
                }

                std::string const &varName = fields[1];

                auto const res = unsetenv(varName.c_str());
                if (res != 0) {
                    auto const err = errno;
                    std::ostringstream ss;
                    ss << "Failed to unset environment variable \"" <<
                        varName << "\": " << strerror(err);
                    printLineError(ss.str());
                    return ErrorExitCode;
                }
            }
            else if (cmd == "prefix" || cmd == "suffix") {
                if (fields.size() != 4) {
                    printInvalidNumParamsError();
                    return ErrorExitCode;
                }

                std::string const &varName = fields[1];
                std::string const &separator = fields[2];
                std::string const &extraVal = fields[3];

                if (separator.size() != 1) {
                    printLineError("Separator must be one character.");
                    return ErrorExitCode;
                }

                char *const origValuePtr = getenv(varName.c_str());

                std::string const origValue =
                    (origValuePtr != nullptr) ? origValuePtr : "";

                std::string const newValue = updateListVar(
                    origValue, extraVal, separator[0], cmd == "prefix");
                
                auto const res = setenv(varName.c_str(), newValue.c_str(), true);
                if (res != 0) {
                    auto const err = errno;
                    std::ostringstream ss;
                    ss << "Failed to set environment variable \"" <<
                        varName << "\": " << strerror(err);
                    printLineError(ss.str());
                    return ErrorExitCode;
                }
            }
            else if (cmd == "cd") {
                if (fields.size() != 2) {
                    printInvalidNumParamsError();
                    return ErrorExitCode;
                }

                std::string const &dirPath = fields[1];

                auto const res = chdir(dirPath.c_str());
                if (res != 0) {
                    auto const err = errno;
                    std::ostringstream ss;
                    ss << "Failed to change directory to \"" <<
                        dirPath << "\": " << strerror(err);
                    printLineError(ss.str());
                    return ErrorExitCode;
                }
            }
            else if (cmd == "argv0") {
                if (fields.size() != 2) {
                    printInvalidNumParamsError();
                    return ErrorExitCode;
                }

                argv0Mode = Argv0Mode::Custom;
                argv0Custom = fields[1];
            }
            else if (cmd == "argv0-wrapper") {
                if (fields.size() != 1) {
                    printInvalidNumParamsError();
                    return ErrorExitCode;
                }

                argv0Mode = Argv0Mode::Wrapper;
            }
            else if (cmd == "argv0-wrapped") {
                if (fields.size() != 1) {
                    printInvalidNumParamsError();
                    return ErrorExitCode;
                }

                argv0Mode = Argv0Mode::Wrapped;
            }
            else if (cmd == "exec") {
                if (fields.size() != 2) {
                    printInvalidNumParamsError();
                    return ErrorExitCode;
                }

                execFound = true;
                execPath = fields[1];
            }
            else if (cmd == "add-args") {
                argsBefore.insert(argsBefore.end(), fields.begin() + 1, fields.end());
            }
            else {
                std::ostringstream ss;
                ss << "Invalid command: \"" << cmd << "\"";
                printLineError(ss.str());
                return ErrorExitCode;
            }
        }

        if (fileStream.bad()) {
            auto const err = errno;
            std::cerr << ErrorPrefix << inputFile <<
                ": failed to read from file: " << strerror(err) << "\n";
            return ErrorExitCode;
        }
    }

    if (!execFound) {
        std::cerr << ErrorPrefix << inputFile << ": No exec command in file.\n";
        return ErrorExitCode;
    }

    // Build the final arguments.
    std::vector<std::string> execArgs;
    // argv0
    execArgs.push_back(
        (argv0Mode == Argv0Mode::Wrapper) ? inputFile :
        (argv0Mode == Argv0Mode::Wrapped) ? execPath :
        /*argv0Mode == Argv0Mode::Custom*/ argv0Custom);
    // arguments specified with add-args
    execArgs.insert(execArgs.end(), argsBefore.begin(), argsBefore.end());
    // arguments on the command line
    execArgs.insert(execArgs.end(), argv + 2, argv + argc);

    // Convert to array of char* as needed for execv.
    std::vector<char *> argsPtrsVec;
    for (std::string const &arg : execArgs) {
        argsPtrsVec.push_back(const_cast<char *>(arg.c_str()));
    }
    argsPtrsVec.push_back(nullptr);

    // Exec, if successful this does not return.
    execv(execPath.c_str(), argsPtrsVec.data());

    // Something went wrong.
    auto const err = errno;
    std::cerr << ErrorPrefix <<
        "Failed to execute \"" << execPath << "\": " << strerror(err) << "\n";
    return ErrorExitCode;
}

int makeWrapper(int argc, char *argv[])
{
    constexpr int MinArgs = 5;

    if (argc < MinArgs) {
        std::cerr << MakeErrorPrefix << "Too few arguments passed.\n";
        return ErrorExitCode;
    }

    std::string const outputFile = argv[2];
    std::string const wrapperPath = argv[3];
    std::string const exec = argv[4];

    std::ofstream fileStream(outputFile);
    if (!fileStream.is_open()) {
        auto const err = errno;
        std::cerr << MakeErrorPrefix << outputFile <<
            ": Failed to open file: " << strerror(err) << "\n";
        return ErrorExitCode;
    }

    fileStream << "#! " << wrapperPath << "\n";

    std::vector<std::string> addArgs;

    for (int argIdx = MinArgs; argIdx < argc; argIdx++) {
        std::string const option = argv[argIdx];

        std::size_t const numRemArgs = argc - argIdx - 1;
        char **const remArgs = argv + argIdx + 1;

        auto const printOptionError = [&](auto const &errText) {
            std::cerr << MakeErrorPrefix << option << ": " << errText << "\n";
        };

        auto const printTooFewArgumentsError = [&]() {
            printOptionError("Too few arguments.");
        };

        if (option == "--argv0-wrapper" || option == "--argv0-wrapped") {
            std::string const command = option.substr(2);
            fileStream << command << "\n";
        }
        else if (option == "--cd" || option == "--argv0" || option == "--unset") {
            std::string const command = option.substr(2);

            if (numRemArgs < 1) {
                printTooFewArgumentsError();
                return ErrorExitCode;
            }

            std::string const argument = remArgs[0];
            argIdx += 1;

            fileStream << command << " " << escapeString(argument) << "\n";
        }
        else if (option == "--set" || option == "--set-default") {
            std::string const command = option.substr(2);

            if (numRemArgs < 2) {
                printTooFewArgumentsError();
                return ErrorExitCode;
            }

            std::string const varName = remArgs[0];
            std::string const value = remArgs[1];
            argIdx += 2;

            fileStream << command << " " <<
                escapeString(varName) << " " << escapeString(value) << "\n";
        }
        else if (option == "--prefix" || option == "--suffix") {
            std::string const command = option.substr(2);

            if (numRemArgs < 3) {
                printTooFewArgumentsError();
                return ErrorExitCode;
            }

            std::string const varName = remArgs[0];
            std::string const separator = remArgs[1];
            std::string const value = remArgs[2];
            argIdx += 3;

            fileStream << command << " " << escapeString(varName) << " " <<
                escapeString(separator) << " " << escapeString(value) << "\n";
        }
        else if (option == "--arg") {
            if (numRemArgs < 1) {
                printTooFewArgumentsError();
                return ErrorExitCode;
            }

            addArgs.push_back(remArgs[0]);
            argIdx += 1;
        }
        else if (option == "--args") {
            if (numRemArgs < 1) {
                printTooFewArgumentsError();
                return ErrorExitCode;
            }

            std::size_t const numArgs = std::stoi(remArgs[0]);
            if (numArgs > numRemArgs - 1) {
                printTooFewArgumentsError();
                return ErrorExitCode;
            }

            addArgs.insert(addArgs.end(), remArgs + 1, remArgs + 1 + numArgs);
            argIdx += 1 + numArgs;
        }
        else {
            printOptionError("Invalid option.");
            return ErrorExitCode;
        }
    }

    if (addArgs.size() > 0) {
        fileStream << "add-args";
        for (std::string const &arg : addArgs) {
            fileStream << " " << escapeString(arg);
        }
        fileStream << "\n";
    }

    fileStream << "exec " << escapeString(exec) << "\n";

    fileStream.close();

    if (fileStream.bad()) {
        auto const err = errno;
        std::cerr << MakeErrorPrefix << outputFile <<
            ": failed to write to file: " << strerror(err) << "\n";
        return ErrorExitCode;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "--make-wrapper") == 0) {
        return makeWrapper(argc, argv);
    } else {
        return runWrapper(argc, argv);
    }
}
