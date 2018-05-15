// @author Tom Svoboda <svobot20@fit.cvut.cz>
#include <iostream>
#include <string>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <cerrno>
#include <vector>
#include <sys/poll.h>
#include <sstream>

#define ECHOPORT                    3999

#define SERVER_KEY                  54621
#define CLIENT_KEY                  45328

#define TIMEOUT                     1
#define TIMEOUT_RECHARGING          5

#define TERMINATING_FIRST_CHAR      '\a'
#define TERMINATING_SECOND_CHAR     '\b'
#define TERMINATING_SEQUENCE_LENGTH 2

#define SERVER_MOVE                 "102 MOVE"
#define SERVER_TURN_LEFT            "103 TURN LEFT"
#define SERVER_TURN_RIGHT           "104 TURN RIGHT"
#define SERVER_PICK_UP              "105 GET MESSAGE"
#define SERVER_LOGOUT               "106 LOGOUT"
#define SERVER_OK                   "200 OK"
#define SERVER_LOGIN_FAILED         "300 LOGIN FAILED"
#define SERVER_SYNTAX_ERROR         "301 SYNTAX ERROR"
#define SERVER_LOGIC_ERROR          "302 LOGIC ERROR"

#define CLIENT_USERNAME_LENGTH      10
#define CLIENT_CONFIRMATION_LENGTH  10
#define CLIENT_OK_LENGTH            10
#define CLIENT_RECHARGING           "RECHARGING"
#define CLIENT_FULL_POWER           "FULL POWER"
#define CLIENT_FULL_POWER_LENGTH    10
#define CLIENT_MESSAGE_LENGTH       98

#define TARGET_X                    (-2)
#define TARGET_Y                    2

bool LOG_ENABLED = false;

enum Direction {
    UNKNOWN,
    UP,
    RIGHT,
    DOWN,
    LEFT
};

struct Position {
    int x;
    int y;
};

/**
 * Client controller
 *
 * manage connection with robot
 * control its moves, so it finds secret message
 */
class ClientController {
public:
    explicit ClientController(int sockfd) {
        this->sockfd = sockfd;

        FD_ZERO(&fdset);
        FD_SET(sockfd, &fdset);
    }

    void handleClientConnection();

private:
    int sockfd;
    fd_set fdset{};
    Position position{};
    Direction direction = UNKNOWN;

    void authenticate();

    uint16_t computeHash(uint16_t key, std::string value);

    void navigate(const Position &target);

    void moveRobot();

    void getPosition();

    void getDirection();

    void updateDirection(const Position &lastPosition, const Position &newPosition);

    void rotateLeft();

    void rotateRight();

    void rotateTo(Direction to);

    void sendResponse(const std::string &msg) const;

    std::string readMsg(int maxMsgSize);

    std::string readFromSocketWithWait(int maxMsgSize, int timeout);

    std::string readFromSocket(int maxMsgSize);

    void updatePosition(const std::string &message);

    bool pickSecretMsg();

    bool positionEquals(const Position &target) const;
};

/**
 * Authorize client
 */
void ClientController::authenticate() {
    if (LOG_ENABLED) {
        std::cout << "== Authenticating socket(" << sockfd << ")" << std::endl;
    }

    std::string userName = readMsg(CLIENT_USERNAME_LENGTH);
    std::string serverHash = std::to_string(computeHash(SERVER_KEY, userName));
    sendResponse(serverHash);

    std::string clientHash = readMsg(CLIENT_CONFIRMATION_LENGTH);
    if (clientHash.length() > 5) {
        sendResponse(SERVER_SYNTAX_ERROR);
        throw std::runtime_error("Authentication failed.");
    }

    for (char c: clientHash) {
        if (!::isdigit(c)) {
            sendResponse(SERVER_SYNTAX_ERROR);
            throw std::runtime_error("Authentication failed.");
        }
    }
    auto clientHashValue = static_cast<uint16_t>(std::stoi(clientHash));
    if (clientHashValue != computeHash(CLIENT_KEY, userName)) {
        sendResponse(SERVER_LOGIN_FAILED);
        throw std::runtime_error("Authentication failed.");
    } else {
        sendResponse(SERVER_OK);
    }

    if (LOG_ENABLED) {
        std::cout << "== Socket(" << sockfd << ") authentication successful" << std::endl;
    }
}

/**
 * Compute hash of string
 * @param key
 * @param value
 * @return
 */
uint16_t ClientController::computeHash(uint16_t key, std::string value) {
    uint16_t result = 0;
    for (char &it : value) {
        result += it;
    }
    result *= 1000;
    result += key;
    return result;
}

/**
 * Move robot towards target position
 * @param target
 */
void ClientController::navigate(const Position &target) {
//    rotate to center
    if (position.y - target.y > 0) {
        rotateTo(DOWN);
        moveRobot();
    } else if (position.y - target.y < 0) {
        rotateTo(UP);
        moveRobot();
    } else if (position.x - target.x < 0) {
        rotateTo(RIGHT);
        moveRobot();
    } else {
        rotateTo(LEFT);
        moveRobot();
    }
}

/**
 * Rotate robot to desired direction
 * @param to - target direction
 */
void ClientController::rotateTo(Direction to) {
    if (direction != to) {
        int next = to - direction;
        if (next > 0) {
            rotateRight();
        } else {
            rotateLeft();
        }

        rotateTo(to);
    }
}

/**
 * Initial move
 */
void ClientController::getPosition() {
    moveRobot();
}

/**
 * Second move to figure direction the robot is heading
 */
void ClientController::getDirection() {
    Position lastPosition{};
    lastPosition.x = position.x;
    lastPosition.y = position.y;
    moveRobot();
    updateDirection(lastPosition, position);
}

/**
 * Set direction based on difference between last two positions
 * @param lastPosition
 * @param newPosition
 */
void ClientController::updateDirection(const Position &lastPosition, const Position &newPosition) {
    if (lastPosition.x == newPosition.x) {
        if (lastPosition.y < newPosition.y) {
            direction = UP;
        } else {
            direction = DOWN;
        }
    } else if (lastPosition.y == newPosition.y) {
        if (lastPosition.x < newPosition.x) {
            direction = RIGHT;
        } else {
            direction = LEFT;
        }
    } else {
        throw std::logic_error("Same position in updateDirection");
    }
}

/**
 * Move client forward
 *
 * Repeats if robot didn't move
 */
void ClientController::moveRobot() {
    Position lastPosition{};
    lastPosition.x = position.x;
    lastPosition.y = position.y;
//    catch robot did not move error
    sendResponse(SERVER_MOVE);
    std::string pos = readMsg(CLIENT_OK_LENGTH);
    updatePosition(pos);
    if (positionEquals(lastPosition)) {
        moveRobot();
    }
}

/**
 * Rotate robot left
 */
void ClientController::rotateLeft() {
    sendResponse(SERVER_TURN_LEFT);
    std::string pos = readMsg(CLIENT_OK_LENGTH);
    updatePosition(pos);
    switch (direction) {
        case UP:
            direction = LEFT;
            break;
        case RIGHT:
            direction = UP;
            break;
        case DOWN :
            direction = RIGHT;
            break;
        case LEFT:
            direction = DOWN;
            break;
        case UNKNOWN:
            break;
    }
}

/**
 * Rotate robot right
 */
void ClientController::rotateRight() {
    sendResponse(SERVER_TURN_RIGHT);
    std::string pos = readMsg(CLIENT_OK_LENGTH);
    updatePosition(pos);
    switch (direction) {
        case UP:
            direction = RIGHT;
            break;
        case RIGHT:
            direction = DOWN;
            break;
        case DOWN :
            direction = LEFT;
            break;
        case LEFT:
            direction = UP;
            break;
        case UNKNOWN:
            break;
    }
}

/**
 * Send message to robot
 * @param msg
 */
void ClientController::sendResponse(const std::string &msg) const {
    if (LOG_ENABLED) {
        std::cout << "To socket(" << sockfd << "): " << msg << std::endl;
    }
    std::string response = msg;
    response.push_back(TERMINATING_FIRST_CHAR);
    response.push_back(TERMINATING_SECOND_CHAR);

    if (send(sockfd, response.c_str(), response.length(), 0) == -1) {
        throw std::runtime_error("Error sending response.");
    }
}

enum ReadMessageState {
    OPEN, // last char was normal character
    CLOSE // last char was first terminating character
};

/**
 * Read message from robot
 * @param maxMsgSize
 * @return
 */
std::string ClientController::readMsg(int maxMsgSize) {
    std::string message = readFromSocketWithWait(maxMsgSize, TIMEOUT);

    if (message == CLIENT_RECHARGING) {
        message = readFromSocketWithWait(CLIENT_FULL_POWER_LENGTH, TIMEOUT_RECHARGING);
        if (message != CLIENT_FULL_POWER) {
            sendResponse(SERVER_LOGIC_ERROR);
            throw std::runtime_error("Expected full power message.");
        }
//        try again
        return readMsg(maxMsgSize);
    };

    return message;
}

/**
 * Read from socket with timeout tolerance
 * @param maxMsgSize
 * @param timeout
 * @return message
 */
std::string ClientController::readFromSocketWithWait(int maxMsgSize, int timeout) {
    struct timeval tv{};
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv);
    return readFromSocket(maxMsgSize);
}

/**
 * Read from socket
 * @param maxMsgSize
 * @return message
 */
std::string ClientController::readFromSocket(int maxMsgSize) {
    std::string message;
    char buffer;
    uint16_t msgLength = 0;
    ReadMessageState state = OPEN;
    bool messageDone = false;
    while (!messageDone) {
        ssize_t readSize = read(sockfd, &buffer, sizeof(buffer));
        msgLength += 1;
        if (maxMsgSize + TERMINATING_SEQUENCE_LENGTH < msgLength) {
            sendResponse(SERVER_SYNTAX_ERROR);
            std::cerr << "Max:" << maxMsgSize << " Curr:" << msgLength << " ReadSize:" << readSize << std::endl;
            std::cout << message << std::endl;
            throw std::runtime_error("Message size out of bounds");
        }
        if (readSize == -1) {
            std::cerr << "Max:" << maxMsgSize << " Curr:" << msgLength << " ReadSize:" << readSize << std::endl;
            std::cout << message << std::endl;
            throw std::runtime_error("Timeout fired");
        }

        switch (buffer) {
            case TERMINATING_FIRST_CHAR:
                if (state == OPEN) {
                    state = CLOSE;
                } else {
                    message.push_back(TERMINATING_FIRST_CHAR);
                }
                break;
            case TERMINATING_SECOND_CHAR:
                if (state == OPEN) {
                    message.push_back(TERMINATING_SECOND_CHAR);
                } else {
                    messageDone = true;
                }
                break;
            default:
                if (state == CLOSE) {
                    state = OPEN;
                    message.push_back(TERMINATING_FIRST_CHAR);
                }
                message.push_back(buffer);
                break;
        }

        if ((maxMsgSize + 1 == msgLength && state == OPEN) || (maxMsgSize + 2 == msgLength && !messageDone)) {
            sendResponse(SERVER_SYNTAX_ERROR);
            std::cout << message << std::endl;
            throw std::runtime_error("Message size out of bounds");
        }
    }

    if (LOG_ENABLED) {
        std::cout << "From socket(" << sockfd << "): " << message << std::endl;
    }

    return message;
}

/**
 * Update current position based on robot response
 * @param message
 */
void ClientController::updatePosition(const std::string &message) {
    std::string type;
    int x;
    int y;

    bool fail;
    std::stringstream ss;
    ss.str(message);

    ss >> type;
    fail = ss.eof() || type != "OK";

    ss >> x;
    fail = fail || ss.eof();

    ss >> y;
    fail = fail || !ss.eof();

    if (fail) {
        sendResponse(SERVER_SYNTAX_ERROR);
        throw std::runtime_error("Confirm message not valid.");
    }

    position.x = x;
    position.y = y;
}


/**
 * Tell robot to pick secret message
 * @return message found
 */
bool ClientController::pickSecretMsg() {
    sendResponse(SERVER_PICK_UP);
    std::string secretMsg = readMsg(CLIENT_MESSAGE_LENGTH);
    if (!secretMsg.empty()) {
        std::cout << "SECRET:" << secretMsg << std::endl;
    }
    return !secretMsg.empty();
}

/**
 * Is robot in target position
 * @param target
 * @return
 */
bool ClientController::positionEquals(const Position &target) const {
    return position.x == target.x && position.y == target.y;
}

/**
 * Control robot
 */
void ClientController::handleClientConnection() {
    try {
        Position target{};
        target.x = TARGET_X;
        target.y = TARGET_Y;
        authenticate();
        getPosition();
        if (!positionEquals(target)) {
            getDirection();
            while (!positionEquals(target)) {
                navigate(target);
            }
        };

//        step grid:
//         0  1  2  3  4
//         9  8  7  6  5
//        10 11 12 13 14
//        19 18 17 16 15
//        20 21 22 23 24

//        -2:2 -1:2 0:2 1:2 2:2
//        -2:1 -1:1 0:1 1:1 2:1
//        -2:0 -1:0 0:0 1:0 2:0
//        ...

//        step 5: x2y1

        int step;
        while (!pickSecretMsg()) {
//            get current step based on position
            int x = (position.x - TARGET_X) % 5;
            int y = (position.y - TARGET_Y) * -1;
            step = y * 5;
            step += y % 2 ? 4 - x : x;
            if (LOG_ENABLED) {
                std::cout << "position:" << position.x << ":" << position.y << "\tstep:" << step;
            }

            step++;
//            compute next position from next step
            if ((step / 5) % 2) {
                target.x = 4 - (step % 5) + TARGET_X;
            } else {
                target.x = (step % 5) + TARGET_X;
            }
            target.y = TARGET_Y - (step / 5);

            if (LOG_ENABLED) {
                std::cout << "\tnext step:" << step << " \ttarget:" << target.x << ":" << target.y << std::endl;
            }

            navigate(target);
        };

//        secret was found
        sendResponse(SERVER_LOGOUT);
    } catch (const std::runtime_error &e) {
        std::cerr << e.what() << std::endl;
    }
}

/**
 * Server manages connections
 */
class Server {
public:
    explicit Server(int port);

    void startListening();

    void stopListening();

    int acceptNextConnection();

private:
    int sockfd = 0;
    sockaddr_in serverAddr{};
};

/**
 * Prepare server to connect on port
 * @param port
 */
Server::Server(int port) {
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port); // NOLINT
}

/**
 * Open server socket
 */
void Server::startListening() {
    sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) {
        throw std::runtime_error("Error creating socket.");
    }

    std::cout << "Connection endpoint created." << std::endl;

    //bind defines the local port and interface address(ip) for the connection
    if (bind(sockfd, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == -1) {
        close(sockfd);
        throw std::runtime_error("Error binding to socket.");
    }

    std::cout << "Binding to endpoint successful." << std::endl;

    // listen for connections on a socket
    if (listen(sockfd, 5) == -1) {
        close(sockfd);
        throw std::runtime_error("Error listening on endpoint");
    }

    std::cout << "Listening for connections." << std::endl << std::endl;
}

/**
 * Close server socket
 */
void Server::stopListening() {
    switch (close(sockfd)) {
        case 0:
            std::cout << "Server stopped" << std::endl;
            break;
        case ENOTCONN:
            std::cerr << "Socket was not connected." << std::endl;
            break;
        default:
            std::cerr << "Socket was not created." << std::endl;
    };

}

/**
 * Accept incoming connection from client
 * @return
 */
int Server::acceptNextConnection() {
    sockaddr_in acceptedAddr{};
    socklen_t acceptedAddrLen = sizeof(acceptedAddr);
    int acceptedSockfd = 0;
    acceptedSockfd = accept(sockfd, (struct sockaddr *) &acceptedAddr, &acceptedAddrLen);

    if (acceptedSockfd == -1) {
        std::cerr << errno << std::endl;
        throw std::runtime_error("Error accepting socket ");
    }

    std::cout << "Accepted connection" << std::endl;
    return acceptedSockfd;
}

int main(int argc, char *argv[]) {
    std::cout << "Close server by terminating process (ctrl+c)." << std::endl;

    int port = ECHOPORT;
    Server server(port);
    server.startListening();

    while (true) {
        int clientSockfd = 0;
        try {
            std::cout << "Waiting for connection" << std::endl;
            clientSockfd = server.acceptNextConnection();
        } catch (const std::runtime_error &e) {
            std::cerr << e.what() << std::endl;
            break;
        }

        int f = fork();
        if (f <= 0) {
//            handle work
            if (f == -1) {
//            fork error
                std::cerr << "Fork failed." << std::endl;
            }

//            handle client connection in child process
            ClientController clientController(clientSockfd);
            clientController.handleClientConnection();

            if (f == 0) {
                return 0; // close child process
            }
        }

//        release connection from parent process (child holds socket as long as it needs)
        close(clientSockfd);
    }
}