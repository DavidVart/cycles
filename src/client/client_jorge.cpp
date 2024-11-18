#include "api.h"
#include "utils.h"
#include <iostream>
#include <queue>
#include <random>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <algorithm>
#include <set>
#include <map>
#include <cmath>
#include <functional>

using namespace cycles;

// Custom comparator for sf::Vector2i
struct Vector2iComparator {
    bool operator()(const sf::Vector2i& lhs, const sf::Vector2i& rhs) const {
        if (lhs.x != rhs.x) {
            return lhs.x < rhs.x;
        }
        return lhs.y < rhs.y;
    }
};

class HybridBotClient {
    Connection connection;
    std::string name;
    GameState state;
    Player my_player;
    std::mt19937 rng;
    int frame_number = 0;
    enum GamePhase { EARLY, MID, LATE } game_phase;

    bool is_valid_move(Direction direction) {
        auto new_pos = my_player.position + getDirectionVector(direction);
        return state.isInsideGrid(new_pos) && state.getGridCell(new_pos) == 0;
    }

    int flood_fill(const sf::Vector2i& start_pos) {
        std::queue<sf::Vector2i> queue;
        std::set<sf::Vector2i, Vector2iComparator> visited; // Use custom comparator
        queue.push(start_pos);
        int area = 0;
        while (!queue.empty()) {
            sf::Vector2i pos = queue.front();
            queue.pop();
            if (!state.isInsideGrid(pos) || visited.count(pos) || state.getGridCell(pos) != 0) {
                continue;
            }
            visited.insert(pos);
            area++;
            for (int dir_value = 0; dir_value < 4; ++dir_value) {
                Direction dir = getDirectionFromValue(dir_value);
                queue.push(pos + getDirectionVector(dir));
            }
        }
        return area;
    }

    double calculate_distance(const sf::Vector2i& pos1, const sf::Vector2i& pos2) const {
        int dx = pos1.x - pos2.x;
        int dy = pos1.y - pos2.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    double min_distance_to_opponents(const sf::Vector2i& position) const {
        double min_dist = std::numeric_limits<double>::max();
        for (const auto& player : state.players) {
            if (player.name == name) continue;
            double dist = calculate_distance(position, player.position);
            if (dist < min_dist) {
                min_dist = dist;
            }
        }
        return min_dist;
    }

    Direction decideMove() {
        // Determine game phase
        if (frame_number < 100) {
            game_phase = EARLY;
        } else if (frame_number < 500) {
            game_phase = MID;
        } else {
            game_phase = LATE;
        }

        std::vector<std::pair<Direction, int>> moves;
        const auto position = my_player.position;
        for (int dir_value = 0; dir_value < 4; ++dir_value) {
            Direction dir = getDirectionFromValue(dir_value);
            if (is_valid_move(dir)) {
                auto new_pos = position + getDirectionVector(dir);
                int open_space = flood_fill(new_pos);
                double distance_to_opponents = min_distance_to_opponents(new_pos);

                int score = 0;
                if (game_phase == EARLY) {
                    score = open_space * 2; // Prioritize open space
                } else if (game_phase == MID) {
                    score = open_space + static_cast<int>(distance_to_opponents / 2); // Balance space and distance
                } else if (game_phase == LATE) {
                    score = -static_cast<int>(distance_to_opponents) + (open_space / 2); // Prioritize trapping opponents
                }

                moves.emplace_back(dir, score);
            }
        }

        if (moves.empty()) {
            spdlog::error("{}: No valid moves available", name);
            exit(1);
        }

        auto best_move = std::max_element(moves.begin(), moves.end(),
                                          [](const std::pair<Direction, int>& a, const std::pair<Direction, int>& b) {
                                              return a.second < b.second;
                                          });

        return best_move->first;
    }

    void receiveGameState() {
        state = connection.receiveGameState();
        frame_number = state.frameNumber;
        for (const auto& player : state.players) {
            if (player.name == name) {
                my_player = player;
                break;
            }
        }
    }

    void sendMove() {
        auto move = decideMove();
        connection.sendMove(move);
    }

public:
    HybridBotClient(const std::string& botName) : name(botName) {
        std::random_device rd;
        rng.seed(rd());
        connection.connect(name);
        if (!connection.isActive()) {
            spdlog::critical("{}: Connection failed", name);
            exit(1);
        }
    }

    void run() {
        while (connection.isActive()) {
            receiveGameState();
            sendMove();
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <bot_name>" << std::endl;
        return 1;
    }

#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_TRACE
    spdlog::set_level(spdlog::level::debug);
#endif

    std::string botName = argv[1];
    HybridBotClient bot(botName);
    bot.run();
    return 0;
}
