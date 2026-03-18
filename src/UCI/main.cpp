#include "UCI/UCI.hpp"

int main() {
    Chess::Init();

    UciHandler uciHandler;

    std::string command;

    while (std::getline(std::cin, command)) {
        if (command.empty()) {
            std::cerr << "Invalid UCI: empty command received" << std::endl; continue;
        }

        else if (command == "quit") {
            uciHandler.HandleCommand("stop");
            break;
        }

        uciHandler.HandleCommand(command);
    }

    return 0;
}