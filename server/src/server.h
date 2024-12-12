#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <vector>

// Structure to represent a game session
struct GameSession {
    int player1 = -1;
    int player2 = -1;
    std::string secretNumber;  // The secret number to guess
    int currentTurn = -1;  // Indicates which player's turn it is: player1 or player2
    std::vector<std::string> moveHistory; // History of valid moves (responses)
};
// Server class definition
class Server {
private:


public:
    void startServer();
    void configureServer();
    void setupSignalHandler();
    void initializeSocket();
    void bindSocket();
    void startListening();
    void eventLoop();
    void handleNewConnection(fd_set &master_set, int &fd_max);
    void handleClientData(int client_socket, fd_set &master_set);
    void processClientMessage(int client_socket, const std::string &message, fd_set &master_set);
    bool isPingMessage(const std::string &message);
    void handleNicknameSetup(int client_socket, const std::string &rawMessage);
    void handleGameMessage(int client_socket, const std::string &rawMessage, fd_set &master_set);
    std::string trimTrailingNewline(const std::string &message);
    std::string sanitizeNickname(const std::string &raw);
    bool isNicknameInUse(const std::string &nickname);
    bool isPlayerTurn(int client_socket, const GameSession &session);
    int isValidGuess(const std::string &guess);
    void handleWinCondition(int winner_socket, GameSession &session);
    void switchPlayerTurn(GameSession &session);
    void sendMessage(int socket, const std::string &message);
    void sendToBothPlayers(const GameSession &session, const std::string &message);
    void handleDisconnect(int client_socket, bool endgame=false);
};

std::string getIPAddress();
void logSessionStatus();
int getMaxSystemConnections();
void signalHandler(int signum);
std::pair<int, int> calculateBullsAndCows(const std::string& guess, const std::string& secret);
std::string generateSecretNumber();

// Function to assign a client to an existing session or create a new one
void assignClientToSession(int client_socket);

#endif // SERVER_H
