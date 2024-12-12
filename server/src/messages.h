#ifndef MESSAGES_H
#define MESSAGES_H

#include <string>

// std::string suc_conn = "Connected. Enter ur nickname and press \"Send\" button\n";
const std::string SUCCESSFUL_CONNECTION = "SC\n";


// std::string disconnectMessage = "Your opponent has disconnected. Waiting for a new player to join...\n";
const std::string OPPONENT_DISCONNECTED = "OD\n";


//"Nickname is already in use. Please choose a different nickname\n"
const std::string NICKNAME_IN_USE = "NIU\n";


//"Nickname successfully set\n"
const std::string NICKNAME_SET = "NS\n";


//"It's not your turn!\n"
const std::string WRONG_TURN = "WT\n";


//"Invalid guess. Please enter a 4-digit number with unique digits.\n"
const std::string INVALID_GUESS = "IG\n" ;

// std::string winMessage = "Congratulations! You guessed the secret number:\n"
const std::string WIN_MSG = "WIN\n";

// std::string loseMessage = "You lost. The secret number was guessed by your opponent.\n"
const std::string LOST_MSG = "LOST\n";

// std::string endGameMessage = "The game has ended. Thank you for playing!\n"
//                              "If you want to continue, enter new nickname\n";
const std::string ENDGAME_MSG = "EG\n";

//"It's your turn\n"
const std::string UR_TURN = "UT\n";

//"It's the opponent's turn\n"
const std::string OPP_TURN = "OT\n";

// std::string gameStartMessage = "The game has started!\n";
const std::string GAME_START = "SG\n";

//"Message must start with 'G'. Disconnecting.\n"
const std::string WRONG_FORMAT = "WF\n";

#endif