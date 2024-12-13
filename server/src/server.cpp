#include "server.h"
#include <iostream>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <csignal>
#include <fcntl.h>
#include <cstring>
#include <sys/select.h>
#include <map>
#include <set>
#include <sys/resource.h>
#include <algorithm>
#include <random>
#include "messages.h"
#include <unordered_map>

/*--------------------------------------------------------GLOBALS------------------------------------------------------------------------------------------------*/
int server_socket;

const int MAX_NICKNAME_LENGTH = 20;
char serverIPAddress[16] = "0.0.0.0"; // Default IP address (all available interfaces)

int32_t SERVER_PORT = 1111; // Default value
int MAX_CONNECTIONS = 5;    // Default value
int8_t USER_TIMEOUT = 30;   // User timeout

std::map<int, std::string> clientNicknames; // Stores socket descriptor to nickname mapping
std::map<int, int> clientSessions;          // Stores socket descriptor to session mapping
std::map<int, GameSession> gameSessions;    // Stores pairs of clients for each session
std::unordered_map<std::string, int> disconnectedClients; // Map of nickname to sessionId


enum GuessValidationCode
{
    VALID_GUESS = 0,
    ERROR_NO_G_PREFIX = -1,
    ERROR_LENGTH = -2,
    ERROR_NOT_DIGITS = -3,
    ERROR_NOT_UNIQUE = -4
};
/* -------------------------------------------------------- SERVER ----------------------------------------------------------------------------------------------*/

void Server::startServer()
{
    // Configuration
    configureServer();
    setupSignalHandler();
    initializeSocket();
    bindSocket();
    startListening();

    // Server logic
    eventLoop();
}

void Server::configureServer()
{
    std::string input;
    // Asking for the IP address from the user
    std::cout << "Enter the IP address for the server (default is " << serverIPAddress << " for all interfaces): ";
    std::getline(std::cin, input);
    if (!input.empty())
    {
        strncpy(serverIPAddress, input.c_str(), sizeof(serverIPAddress) - 1);
        serverIPAddress[sizeof(serverIPAddress) - 1] = '\0'; // Ensure null-termination
    }

    std::cout << "Enter the port number for the server (default is 1111): ";
    std::getline(std::cin, input);
    if (!input.empty())
    {
        try
        {
            int port = std::stoi(input);
            if (port < 0 || port > 65535)
            {
                throw std::out_of_range("Port number out of range.");
            }
            SERVER_PORT = port;
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Error] Invalid port number: " << e.what() << ". Please enter a valid port between 0 and 65535.\n";
            exit(5);
        }
    }

    std::cout << "Enter the maximum number of connections (default is 5, or press ENTER to use system limit): ";
    std::getline(std::cin, input);
    if (!input.empty())
    {
        MAX_CONNECTIONS = std::stoi(input);
    }
    else
    {
        MAX_CONNECTIONS = getMaxSystemConnections();
        std::cout << "[Server] System limit for maximum connections is: " << MAX_CONNECTIONS << "\n";
    }
}

void Server::setupSignalHandler()
{
    signal(SIGINT, signalHandler);
}

void Server::initializeSocket()
{
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        std::cerr << "[Server] Socket creation failed\n";
        exit(EXIT_FAILURE);
    }

    // Set server socket to be non-blocking
    fcntl(server_socket, F_SETFL, O_NONBLOCK);
}

void Server::bindSocket()
{
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    // Convert IP address to binary form
    if (inet_pton(AF_INET, serverIPAddress, &server_addr.sin_addr) <= 0)
    {
        std::cerr << "[Server] Invalid IP address: " << serverIPAddress << ". Failed to bind socket.\n";
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "[Server] Socket binding failed\n";
        close(server_socket);
        exit(EXIT_FAILURE);
    }
}

void Server::startListening()
{
    if (listen(server_socket, MAX_CONNECTIONS) < 0)
    {
        std::cerr << "[Server] Listen failed\n";
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    std::string ipAddress = getIPAddress();
    std::cout << "[Server] Server is running on IP: " << ipAddress << ", Port: " << SERVER_PORT << "\n";
    std::cout << "[Server] Maximum allowed connections: " << MAX_CONNECTIONS << "\n";
}

void Server::eventLoop()
{
    fd_set master_set, read_fds;
    int fd_max = server_socket;

    // Initialize the master set and add the server socket
    FD_ZERO(&master_set);
    FD_SET(server_socket, &master_set);

    // To track the last activity of each client
    std::map<int, time_t> lastActivity;

    while (true)
    {
        read_fds = master_set;

        // Set a timeout of 30 seconds
        struct timeval timeout;
        timeout.tv_sec = USER_TIMEOUT;
        timeout.tv_usec = 0;

        // Use select to wait for activity on any socket, with a timeout
        int activity = select(fd_max + 1, &read_fds, nullptr, nullptr, &timeout);
        if (activity == -1)
        {
            std::cerr << "[Server] Select failed\n";
            break;
        }

        // Check if timeout occurred
        time_t currentTime = time(nullptr);
        for (auto it = lastActivity.begin(); it != lastActivity.end();)
        {
            if (difftime(currentTime, it->second) > USER_TIMEOUT)
            {
                // Disconnect client if inactive for more than USER_TIMEOUT seconds
                int inactiveSocket = it->first;
                std::cout << "[Server] Disconnecting socket " << inactiveSocket << " due to inactivity\n";
                handleDisconnect(inactiveSocket);
                FD_CLR(inactiveSocket, &master_set);
                close(inactiveSocket);
                it = lastActivity.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Iterate through file descriptors to see which one is ready
        for (int i = 0; i <= fd_max; ++i)
        {
            if (FD_ISSET(i, &read_fds))
            {
                if (i == server_socket)
                {
                    handleNewConnection(master_set, fd_max);
                    lastActivity[fd_max] = time(nullptr); // Record the time of new connectioF
                }
                else
                {
                    handleClientData(i, master_set);
                    lastActivity[i] = time(nullptr); // Update activity timestamp
                }
            }
        }
    }

    close(server_socket);
}

void Server::handleNewConnection(fd_set &master_set, int &fd_max)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

    if (client_socket == -1)
    {
        std::cerr << "[Server] Accept failed\n";
    }
    else
    {
        // Set client socket to non-blocking
        fcntl(client_socket, F_SETFL, O_NONBLOCK);

        // Add the new client socket to the master set
        FD_SET(client_socket, &master_set);
        if (client_socket > fd_max)
        {
            fd_max = client_socket;
        }
        std::cout << "[Server] New connection from " << inet_ntoa(client_addr.sin_addr) << " on socket " << client_socket << "\n";

        // Check if the client can reconnect to an existing session
        std::string placeholderNickname = ""; // Placeholder for now until we receive the nickname

        // If the client wasn't reconnected, treat them as a new client
        if (clientNicknames[client_socket].empty())
        {
            clientNicknames[client_socket] = ""; // Placeholder for nickname until received
        }

        // send(client_socket, SUCCESSFUL_CONNECTION.c_str(), SUCCESSFUL_CONNECTION.length(), 0);
        sendMessage(client_socket, SUCCESSFUL_CONNECTION);
    }
}

void Server::handleClientData(int client_socket, fd_set &master_set)
{
    char buffer[256];
    memset(buffer, 0, sizeof(buffer));
    int nbytes = recv(client_socket, buffer, sizeof(buffer), 0);

    // If recv returns 0 or an error, the client disconnected
    if (nbytes <= 0)
    {
        if (nbytes == 0)
        {
            std::cout << "[Server] Socket " << client_socket << " disconnected\n";
        }
        else
        {
            std::cerr << "[Server] Recv error on socket " << client_socket << "\n";
        }
        close(client_socket);
        FD_CLR(client_socket, &master_set);

        // Instead of directly erasing data, handle disconnect logic
        handleDisconnect(client_socket);
    }
    // Otherwise, we received data from the client
    else
    {
        processClientMessage(client_socket, std::string(buffer), master_set);
    }
}

void Server::processClientMessage(int client_socket, const std::string &message, fd_set &master_set)
{
    if (isPingMessage(message))
    {
        return;
    }

    if (clientNicknames[client_socket].empty())
    {
        handleNicknameSetup(client_socket, message);
    }
    else
    {
        handleGameMessage(client_socket, message, master_set);
    }
    logSessionStatus();

}

// --------------------- MESSAGE PROCESSING UTILS ---------------------------------------------------------------------------------------------

bool Server::isPingMessage(const std::string &message)
{
    return message.find("PING") != std::string::npos;
}

void Server::handleNicknameSetup(int client_socket, const std::string &rawMessage)
{
    std::string nickname = sanitizeNickname(rawMessage);
    if (nickname.empty())
    {
        return;
    }

    if (isNicknameInUse(nickname))
    {
        sendMessage(client_socket, NICKNAME_IN_USE);
    }
    else
    {
        clientNicknames[client_socket] = nickname;
        std::cout << "[Server] Client on socket " << client_socket << " set nickname: " << nickname << "\n";
        sendMessage(client_socket, NICKNAME_SET);
        assignClientToSession(client_socket);
    }
}

void Server::handleGameMessage(int client_socket, const std::string &rawMessage, fd_set &master_set)
{
    std::string procMessage = trimTrailingNewline(rawMessage);

    int sessionId = clientSessions[client_socket];
    static std::map<int, int> wrongTurnAttempts; // Tracks the number of wrong turn attempts per client
    
    GameSession &session = gameSessions[sessionId];

    if (!isPlayerTurn(client_socket, session) || session.player1 == -1 || session.player2 == -1)
    {
        wrongTurnAttempts[client_socket]++;
        std::cout << "[Server] Received wrong message from socket " << client_socket << "\n";

        if (wrongTurnAttempts[client_socket] >= 3) {
            std::cout << "[Server] Client on socket " << client_socket << " exceeded wrong turn limit. Disconnecting...\n";
            sendMessage(client_socket, WRONG_FORMAT);
            handleDisconnect(client_socket, false);
            FD_CLR(client_socket, &master_set);
            close(client_socket);
            wrongTurnAttempts.erase(client_socket);
            return;
        }

        sendMessage(client_socket, WRONG_TURN);
        return;
    }

    // Reset the wrong turn counter if the player makes a valid move
    wrongTurnAttempts[client_socket] = 0;

    std::cout << "[Server] Received message from socket " << client_socket << ": " << procMessage << "\n";

    int validationCode = isValidGuess(procMessage);
    if (validationCode != VALID_GUESS)
    {
        if (validationCode == ERROR_NO_G_PREFIX)
        {
            sendMessage(client_socket, WRONG_FORMAT);
            handleDisconnect(client_socket);
            FD_CLR(client_socket, &master_set);
            close(client_socket);
            return;
        }
        else
        {
            sendMessage(client_socket, INVALID_GUESS);
            return;
        }
    }

    std::string guessDigits = procMessage.substr(1);

    auto result = calculateBullsAndCows(guessDigits, session.secretNumber);
    //================================================VALID GUESS RESPONSE
    std::string response = procMessage +
                           "B" + std::to_string(result.first) +
                           "C" + std::to_string(result.second) + "\n";

    session.moveHistory.push_back(response);
    sendToBothPlayers(session, response);

    if (result.first == 4)
    {
        handleWinCondition(client_socket, session);
        return;
    }

    switchPlayerTurn(session);
}

// -------------------------------------------------------------------------------------------------------------------------------------------------------------------

std::string Server::trimTrailingNewline(const std::string &message)
{
    std::string result = message;
    if (!result.empty() && result.back() == '\n')
    {
        result.pop_back();
    }
    return result;
}

std::string Server::sanitizeNickname(const std::string &raw)
{
    std::string nickname = raw;
    if (!nickname.empty() && nickname.back() == '\n')
    {
        nickname.pop_back();
    }
    nickname = nickname.substr(0, MAX_NICKNAME_LENGTH);
    return nickname;
}

bool Server::isNicknameInUse(const std::string &nickname)
{
    for (const auto &pair : clientNicknames)
    {
        if (pair.second == nickname)
        {
            return true;
        }
    }
    return false;
}

bool Server::isPlayerTurn(int client_socket, const GameSession &session)
{
    return (client_socket == session.currentTurn);
}

int Server::isValidGuess(const std::string &guess)
{
    if (guess.empty() || guess[0] != 'G')
    {
        return ERROR_NO_G_PREFIX;
    }

    std::string digitsPart = guess.substr(1);

    if (digitsPart.length() != 4)
        return ERROR_LENGTH;
    if (!std::all_of(digitsPart.begin(), digitsPart.end(), ::isdigit))
        return ERROR_NOT_DIGITS;

    std::set<char> digitsSet(digitsPart.begin(), digitsPart.end());
    if (digitsSet.size() != 4)
        return ERROR_NOT_UNIQUE;

    return VALID_GUESS;
}

void Server::handleWinCondition(int winner_socket, GameSession &session)
{
    std::cout << "[Server] Player on socket " << winner_socket << " guessed the number!" << std::endl;

    int opponent_socket = (session.player1 == winner_socket) ? session.player2 : session.player1;

    sendMessage(winner_socket, WIN_MSG);

    sendMessage(opponent_socket, LOST_MSG);

    sendMessage(winner_socket, ENDGAME_MSG);
    sendMessage(opponent_socket, ENDGAME_MSG);

    handleDisconnect(winner_socket, true);
    handleDisconnect(opponent_socket, true);
}

void Server::switchPlayerTurn(GameSession &session)
{
    session.currentTurn = (session.currentTurn == session.player1) ? session.player2 : session.player1;

    int current_player = session.currentTurn;
    int opponent_player = (current_player == session.player1) ? session.player2 : session.player1;

    sendMessage(current_player, UR_TURN);
    if (opponent_player != -1)
    {
        sendMessage(opponent_player, OPP_TURN);
    }
}

void Server::sendMessage(int socket, const std::string &message)
{
    send(socket, message.c_str(), message.length(), 0);
}

void Server::sendToBothPlayers(const GameSession &session, const std::string &message)
{
    sendMessage(session.player1, message);
    sendMessage(session.player2, message);
}

void Server::handleDisconnect(int client_socket, bool endgame)
{
    // Check if the client is in a session
    if (clientSessions.find(client_socket) != clientSessions.end())
    {
        int sessionId = clientSessions[client_socket];
        GameSession &session = gameSessions[sessionId];
        int opponent_socket = (session.player1 == client_socket) ? session.player2 : session.player1;

        // Notify the opponent if the player disconnects
        if (!endgame && opponent_socket != -1)
        {
            sendMessage(opponent_socket, OPPONENT_DISCONNECTED);
        }

        // Save the nickname and sessionId to disconnectedClients if session is still active
        std::string nickname = clientNicknames[client_socket];
        if (opponent_socket != -1)
        {
            disconnectedClients[nickname] = sessionId; // Save the disconnected client's data
        }

        // Remove the disconnected player from the session
        if (session.player1 == client_socket)
        {
            session.player1 = -1;
        }
        else
        {
            session.player2 = -1;
        }

        // If both players are disconnected, delete the session
        if (session.player1 == -1 && session.player2 == -1)
        {
            std::cout << "[Server] Both players have disconnected. Removing session " << sessionId << "\n";
            gameSessions.erase(sessionId);

            // Remove both players from the disconnectedClients map
            for (auto it = disconnectedClients.begin(); it != disconnectedClients.end(); )
            {
                if (it->second == sessionId)
                {
                    it = disconnectedClients.erase(it); // Erase and get the next iterator
                }
                else
                {
                    ++it;
                }
            }
        }


        // Clean up client data
        clientNicknames.erase(client_socket);
        clientSessions.erase(client_socket);
    }

    logSessionStatus();
}
/* ------------------------------------------------------------ UTIL FUNCTIONS ---------------------------------------------------------------------*/
std::string generateSecretNumber()
{
    std::string number;
    std::random_device rd;                      // Obtain a random seed from hardware
    std::mt19937 gen(rd());                     // Seed the generator
    std::uniform_int_distribution<> dist(0, 9); // Define the range [0, 9]

    while (number.length() < 4)
    {
        char digit = '0' + dist(gen);
        // Ensure all digits are unique
        if (number.find(digit) == std::string::npos)
        {
            number += digit;
        }
    }
    return number;
}

std::pair<int, int> calculateBullsAndCows(const std::string &guess, const std::string &secret)
{
    int bulls = 0, cows = 0;

    for (int i = 0; i < guess.size(); ++i)
    {
        if (guess[i] == secret[i])
        {
            bulls++;
        }
        else if (secret.find(guess[i]) != std::string::npos)
        {
            cows++;
        }
    }

    return {bulls, cows};
}

// Function to get system limit for maximum connections
int getMaxSystemConnections()
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0)
    {
        return limit.rlim_cur;
    }
    else
    {
        return MAX_CONNECTIONS; // Default if unable to retrieve limit
    }
}

void signalHandler(int signum)
{
    std::cout << "Interrupt signal (" << signum << ") received. Closing server socket..." << std::endl;
    close(server_socket);
    exit(signum);
}

// Function to get the IP address of the server
std::string getIPAddress()
{
    struct ifaddrs *ifAddrStruct = nullptr;
    void *tmpAddrPtr = nullptr;
    std::string ipAddress = "127.0.0.1"; // Default to localhost

    getifaddrs(&ifAddrStruct);

    for (struct ifaddrs *ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr)
        {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET)
        { // Check if it's IPv4
            tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            if (std::string(ifa->ifa_name) != "lo")
            { // Skip loopback address
                ipAddress = addressBuffer;
                break;
            }
        }
    }

    if (ifAddrStruct != nullptr)
    {
        freeifaddrs(ifAddrStruct);
    }

    return ipAddress;
}

void logSessionStatus()
{
    std::cout << "===== Current Session Status =====" << std::endl;

    for (const auto &session : gameSessions)
    {
        const GameSession &currentSession = session.second;
        std::cout << "Session ID: " << session.first << std::endl;
        std::cout << " - Player 1 Socket: " << currentSession.player1 << (currentSession.player1 != -1 ? " (Connected)" : " (Disconnected)") << std::endl;
        std::cout << " - Player 2 Socket: " << currentSession.player2 << (currentSession.player2 != -1 ? " (Connected)" : " (Disconnected)") << std::endl;
        std::cout << " - Current Turn: " << (currentSession.currentTurn == currentSession.player1 ? "Player 1" : "Player 2") << std::endl;
        std::cout << " - Secret Number: " << currentSession.secretNumber << std::endl;
    }

    std::cout << "==================================" << std::endl;
}

void assignClientToSession(int client_socket)
{
    std::string clientNickname = clientNicknames[client_socket];
    bool sessionAssigned = false;

    // Check if the client is in the disconnectedClients map
    if (disconnectedClients.find(clientNickname) != disconnectedClients.end())
    {
        int sessionId = disconnectedClients[clientNickname]; // Retrieve session ID
        GameSession &session = gameSessions[sessionId];

        if (session.player1 == -1)
        {
            session.player1 = client_socket;
        }
        else
        {
            session.player2 = client_socket;
        }

        clientSessions[client_socket] = sessionId; // Map client socket to session ID
        disconnectedClients.erase(clientNickname); // Remove from disconnectedClients
        sessionAssigned = true;

        // Notify the reconnected player and send the move history
        std::cout << "[Server] Client with nickname " << clientNickname << " rejoined session " << sessionId << "\n";

        for (const auto &move : session.moveHistory)
        {
            send(client_socket, move.c_str(), move.length(), 0);
        }

        // Notify about turns
        int currentTurnPlayer = session.currentTurn;
        send(currentTurnPlayer, UR_TURN.c_str(), UR_TURN.length(), 0);

        int opponentPlayer = (currentTurnPlayer == session.player1) ? session.player2 : session.player1;
        if (opponentPlayer != -1)
        {
            send(opponentPlayer, OPP_TURN.c_str(), OPP_TURN.length(), 0);
        }
    }
    else
    {
        // Check for an existing session with only one active player
        for (auto &session : gameSessions)
        {
            // Ensure the session is not in the list of disconnectedClients
            bool sessionInDisconnectedClients = false;
            for (const auto &disconnectedPair : disconnectedClients)
            {
                if (disconnectedPair.second == session.first)
                {
                    sessionInDisconnectedClients = true;
                    break;
                }
            }

            if (!sessionInDisconnectedClients && session.second.player1 != -1 && session.second.player2 == -1)
            {
                session.second.player2 = client_socket; // Assign the client to player2
                clientSessions[client_socket] = session.first;
                sessionAssigned = true;

                std::cout << "[Server] Client on socket " << client_socket << " joined session " << session.first << " as player2\n";

                // Notify the players about the game start
                send(session.second.player1, GAME_START.c_str(), GAME_START.length(), 0);
                send(client_socket, GAME_START.c_str(), GAME_START.length(), 0);

                // Set the initial turn
                session.second.currentTurn = session.second.player1;

                send(session.second.player1, UR_TURN.c_str(), UR_TURN.length(), 0);
                send(client_socket, OPP_TURN.c_str(), OPP_TURN.length(), 0);

                break;
            }
        }

        // If no existing session is available, create a new session
        if (!sessionAssigned)
        {
            int newSessionId = gameSessions.size();
            GameSession newSession;
            newSession.player1 = client_socket;
            newSession.currentTurn = client_socket;
            newSession.secretNumber = generateSecretNumber();

            gameSessions[newSessionId] = newSession;
            clientSessions[client_socket] = newSessionId;

            std::cout << "[Server] New game session " << newSessionId << " created for client " << client_socket << "\n";
            std::cout << "[Server] Waiting for a second player to join session " << newSessionId << "\n";
        }
    }

    logSessionStatus();
}
