/*
    Command Line Tetris
    Cross-platform version for Windows / macOS / Linux

    license: GNU GPLv3

    Controls:
      Left / Right / Down : Move
      Z                  : Rotate
      Q / Esc            : Quit
*/

#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <stdlib.h>


#ifdef _WIN32
    #include <conio.h>
    #include <windows.h>
#else
    #include <termios.h>
    #include <unistd.h>
    #include <sys/select.h>
#endif

using namespace std;

// -----------------------------------------------------------------------------
// Cross-platform terminal handling
// -----------------------------------------------------------------------------

#ifndef _WIN32
static termios g_originalTermios;
static bool g_terminalConfigured = false;

int nScreenWidth = 80;
int nScreenHeight = 30;
string tetromino[7];
int nFieldWidth = 12;
int nFieldHeight = 18;
unsigned char* pField = nullptr;
char* pDisplayBuffer = nullptr;
int nDisplayBufferPos = 0;
const int kMaxDisplayBufferSize = 1024*1024;

void restore_terminal()
{
    if (g_terminalConfigured)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_originalTermios);
        cout << "\x1b[?25h" << "\x1b[0m" << flush; // show cursor
        g_terminalConfigured = false;

        printf("\033[?25h"); // 恢复光标
    }
    printf("\033[?25h"); // 恢复光标
    fflush(stdout);
}

void configure_terminal()
{
    tcgetattr(STDIN_FILENO, &g_originalTermios);

    termios raw = g_originalTermios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    g_terminalConfigured = true;

    cout << "\x1b[?25l" << flush; // hide cursor
}

bool key_available()
{
    timeval tv{0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
}

int read_key()
{
    if (!key_available())
        return -1;

    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1)
        return -1;

    // Escape sequence: arrows are usually ESC [ A/B/C/D.
    if (c == 27)
    {
        if (!key_available())
            return 27;

        unsigned char c2 = 0;
        if (read(STDIN_FILENO, &c2, 1) != 1)
            return 27;

        if (c2 == '[')
        {
            if (!key_available())
                return 27;

            unsigned char c3 = 0;
            if (read(STDIN_FILENO, &c3, 1) != 1)
                return 27;

            switch (c3)
            {
                case 'A': return 'U'; // Up
                case 'B': return 'D'; // Down
                case 'C': return 'R'; // Right
                case 'D': return 'L'; // Left
            }
        }

        return 27;
    }

    return c;
}
#else
void restore_terminal()
{
    cout << "\x1b[?25h" << "\x1b[0m" << flush;
}

void configure_terminal()
{
    // Windows 10+ supports ANSI escape sequences in the console.
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode))
        {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }

    cout << "\x1b[?25l" << flush; // hide cursor
}

bool key_available()
{
    return _kbhit() != 0;
}

int read_key()
{
    if (!_kbhit())
        return -1;

    int c = _getch();

    // Windows arrow/function keys start with 0 or 224.
    if (c == 0 || c == 224)
    {
        int c2 = _getch();

        switch (c2)
        {
            case 72: return 'U'; // Up
            case 80: return 'D'; // Down
            case 75: return 'L'; // Left
            case 77: return 'R'; // Right
        }

        return -1;
    }

    return c;
}
#endif

void clear_screen()
{
    cout << "\x1b[2J\x1b[H";
}

void cleanup()
{
    restore_terminal();

    delete[] pField;
    pField = nullptr;
}

int rotate(int px, int py, int r)
{
    int pi = 0;

    switch (r % 4)
    {
        case 0:
            pi = py * 4 + px;
            break;

        case 1:
            pi = 12 + py - (px * 4);
            break;

        case 2:
            pi = 15 - (py * 4) - px;
            break;

        case 3:
            pi = 3 - py + (px * 4);
            break;
    }

    return pi;
}

bool does_piece_fit(int nTetromino, int nRotation, int nPosX, int nPosY)
{
    for (int px = 0; px < 4; px++) {
        for (int py = 0; py < 4; py++) {
            int pi = rotate(px, py, nRotation);
            int fi = (nPosY + py) * nFieldWidth + (nPosX + px);

            if (nPosX + px >= 0 && nPosX + px < nFieldWidth) {
                if (nPosY + py >= 0 && nPosY + py < nFieldHeight) {
                    if (tetromino[nTetromino][pi] != '.' && pField[fi] != 0)
                        return false;
                }
            }
        }
    }

    return true;
}

void init() {
    // Make sure terminal settings are restored even if the program exits normally.
    atexit(cleanup);

    srand(static_cast<unsigned int>(time(nullptr)));

    tetromino[0] = "..X...X...X...X.";
    tetromino[1] = "..X..XX...X.....";
    tetromino[2] = ".....XX..XX.....";
    tetromino[3] = "..X..XX..X......";
    tetromino[4] = ".X...XX...X.....";
    tetromino[5] = ".X...X...XX.....";
    tetromino[6] = "..X...X..XX.....";

    pField = new unsigned char[nFieldWidth * nFieldHeight];

    for (int x = 0; x < nFieldWidth; x++) {
        for (int y = 0; y < nFieldHeight; y++) {
            pField[y * nFieldWidth + x] =
                (x == 0 || x == nFieldWidth - 1 || y == nFieldHeight - 1) ? 9 : 0;
        }
    }

    pDisplayBuffer = new char[kMaxDisplayBufferSize];

    configure_terminal();
    clear_screen();
}

void rhythm(int& nSpeedCount, int nSpeed, bool& bForceDown) {
    // Timing
    this_thread::sleep_for(chrono::milliseconds(50));
    nSpeedCount++;
    bForceDown = (nSpeedCount == nSpeed);
}

void handle_key_input(bool* bKey, size_t bKey_size, bool& bGameOver) {
    // Input
    memset(bKey, 0, bKey_size);

    // Drain currently available input so key presses are handled promptly.
    while (key_available()) {
        int key = read_key();

        if (key == 'L') {
            bKey[1] = true;
        } else if (key == 'R') {
            bKey[0] = true;
        } else if (key == 'D') {
            bKey[2] = true;
        } else if (key == 'z' || key == 'Z') {
            bKey[3] = true;
        } else if (key == 'q' || key == 'Q' || key == 27) {
            bGameOver = true;
        }
    }
}

void handle_player_movement(bool* bKey, int& nCurrentX, int& nCurrentY, int nCurrentPiece, int nCurrentRotation) {
    if (bKey[0] &&
        does_piece_fit(nCurrentPiece, nCurrentRotation, nCurrentX + 1, nCurrentY)) {
        nCurrentX++;
    }

    if (bKey[1] &&
        does_piece_fit(nCurrentPiece, nCurrentRotation, nCurrentX - 1, nCurrentY)) {
        nCurrentX--;
    }

    if (bKey[2] &&
        does_piece_fit(nCurrentPiece, nCurrentRotation, nCurrentX, nCurrentY + 1)) {
        nCurrentY++;
    }
}
 
void rotate_piece(bool* bKey, int& nCurrentRotation, int nCurrentPiece, int nCurrentX, int nCurrentY, bool& bRotateHold) {
    // Rotate, but latch to stop wild spinning
    if (bKey[3]) {
        if (bRotateHold &&
            does_piece_fit(nCurrentPiece, nCurrentRotation + 1, nCurrentX, nCurrentY)) {
            nCurrentRotation++;
        }

        bRotateHold = false;
    } else {
        bRotateHold = true;
    }
}

void piece_down(bool& bForceDown, int& nSpeedCount, int& nPieceCount, int& nSpeed, int& nScore, vector<int>& vLines, bool& bGameOver,
                int& nCurrentY, int& nCurrentPiece, int& nCurrentRotation, int& nCurrentX) {
    // Force the piece down the playfield if it's time
    if (bForceDown) {
        nSpeedCount = 0;
        nPieceCount++;

        if (nPieceCount % 50 == 0) {
            if (nSpeed >= 10) {
                nSpeed--;
            }
        }

        if (does_piece_fit(nCurrentPiece, nCurrentRotation, nCurrentX, nCurrentY + 1)) {
            nCurrentY++;
        } else {
            // Lock the piece in place
            for (int px = 0; px < 4; px++) {
                for (int py = 0; py < 4; py++) {
                    if (tetromino[nCurrentPiece][rotate(px, py, nCurrentRotation)] != '.') {
                        int x = nCurrentX + px;
                        int y = nCurrentY + py;

                        if (x >= 0 && x < nFieldWidth &&
                            y >= 0 && y < nFieldHeight) {
                            pField[y * nFieldWidth + x] = nCurrentPiece + 1;
                        }
                    }
                }
            }

            // Check for lines
            for (int py = 0; py < 4; py++) {
                if (nCurrentY + py < nFieldHeight - 1) {
                    bool bLine = true;

                    for (int px = 1; px < nFieldWidth - 1; px++) {
                        bLine &= (pField[(nCurrentY + py) * nFieldWidth + px]) != 0;
                    }

                    if (bLine) {
                        for (int px = 1; px < nFieldWidth - 1; px++) {
                            pField[(nCurrentY + py) * nFieldWidth + px] = 8;
                        }

                        vLines.push_back(nCurrentY + py);
                    }
                }
            }

            nScore += 25;

            if (!vLines.empty()) {
                nScore += (1 << static_cast<int>(vLines.size())) * 100;
            }

            // Pick new piece
            nCurrentX = nFieldWidth / 2 - 2;
            nCurrentY = 0;
            nCurrentRotation = 0;
            nCurrentPiece = rand() % 7;

            if (!does_piece_fit(nCurrentPiece, nCurrentRotation, nCurrentX, nCurrentY)) {
                bGameOver = true;
            }
        }
    }
}

void draw_border_unit(int x, int y) {
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[%d;%dH", y, x);
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[38;2;12;85;98m");  // 前景色：亮青色
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[48;2;6;10;18m");    // 背景色：深蓝黑色
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "▓");
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[0m");                // 重置颜色
}

void draw_border(vector<char>& screen) {
    int nDisplayFieldWidth = nFieldWidth * 2 - 2;

    for (int x = 0; x < nDisplayFieldWidth; x++) {
        for (int y = 0; y < nFieldHeight; y++) {
            if ( x == 0 || x == nDisplayFieldWidth - 1 || y == nFieldHeight - 1) {
                //screen[(y + 2) * nScreenWidth + (x + 2)] = '#';
                draw_border_unit(x+3, y+3);
            }
        }
    }
}

typedef struct {
    int r, g, b;
} Color;

typedef struct {
    Color dark;
    Color light;
} BlockColor;

BlockColor colors[8] = {
    {{128, 128, 128}, {255, 255, 255}},
    {{  8, 145, 178}, {103, 232, 249}}, // I 青色
    {{202, 138,   4}, {253, 224,  71}}, // O 金黄
    {{126,  34, 206}, {216, 180, 254}}, // T 紫色
    {{ 21, 128,  61}, {134, 239, 172}}, // S 绿色
    {{190,  18,  60}, {253, 164, 175}}, // Z 红色
    {{ 29,  78, 216}, {147, 197, 253}}, // J 蓝色
    {{194,  65,  12}, {253, 186, 116}}  // L 橙色
};

Color kCyan   = {34, 211, 238};
Color kPurple = {167, 139, 250};
Color kYellow = {250, 204, 21};
Color kBlack  = {15, 23, 42};

void draw_one_unit(int x, int y, Color top, Color bottom) {
    // nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[%d;%dH", y, x);
    // nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[38;2;%d;%d;%dm", top.r, top.g, top.b);
    // nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[48;2;%d;%d;%dm", bottom.r, bottom.g, bottom.b);
    // nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "▀▀");
    // nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[0m");

    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[%d;%dH", y, x);
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[38;2;%d;%d;%dm", top.r, top.g, top.b);
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[48;2;%d;%d;%dm", bottom.r, bottom.g, bottom.b);
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "▀");
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[38;2;%d;%d;%dm", bottom.r, bottom.g, bottom.b);
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[48;2;%d;%d;%dm", top.r, top.g, top.b);
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "▀");
    nDisplayBufferPos += snprintf(pDisplayBuffer + nDisplayBufferPos, kMaxDisplayBufferSize - nDisplayBufferPos, "\033[0m");
}

void display(vector<char>& screen, int nCurrentX, int nCurrentY, int nCurrentPiece, int nCurrentRotation, int nScore) {
    // Display
    fill(screen.begin(), screen.end(), ' ');

    // Draw field
    for (int x = 0; x < nFieldWidth; x++) {
        for (int y = 0; y < nFieldHeight; y++) {
            // screen[(y + 2) * nScreenWidth + (x + 2)] =
            //     " ABCDEFG= "[pField[y * nFieldWidth + x]];

            auto current_char = pField[y * nFieldWidth + x];
            if (current_char >= 1 && current_char <= 7 ) {
                BlockColor& color = colors[current_char];
                draw_one_unit((x+2)*2-2, y+3, color.dark, color.light);
            }
        }
    }

    draw_border(screen);

    // Draw current piece
    for (int px = 0; px < 4; px++) {
        for (int py = 0; py < 4; py++) {
            if (tetromino[nCurrentPiece][rotate(px, py, nCurrentRotation)] != '.') {
                int sx = nCurrentX + px + 2;
                int sy = nCurrentY + py + 2;

                if (sx >= 0 && sx < nScreenWidth &&
                    sy >= 0 && sy < nScreenHeight) {
                    // screen[sy * nScreenWidth + sx] =
                    //     static_cast<char>('A' + nCurrentPiece);
                    BlockColor& color = colors[nCurrentPiece+1];
                    draw_one_unit(sx*2-2, sy+1, color.dark, color.light);
                }
            }
        }
    }

    // Draw score
    char scoreText[32];
    snprintf(scoreText, sizeof(scoreText), "SCORE: %8d", nScore);

    int scoreX = nFieldWidth *2 + 6;
    int scoreY = 2;

    for (int i = 0; scoreText[i] != '\0' &&
                    scoreX + i < nScreenWidth; i++) {
        screen[scoreY * nScreenWidth + scoreX + i] = scoreText[i];
    }

    const char title[] = "Linger's Tetris Game";
    for (int i = 0; i < sizeof(title); ++i) {
        screen[nFieldWidth*2 + i] = title[i];
    }
}

void handle_line_completion(vector<int>& vLines, vector<char>& screen) {
    // Animate line completion
    if (!vLines.empty()) {
        cout << "\x1b[H";

        for (int y = 0; y < nScreenHeight; y++) {
            cout.write(&screen[y * nScreenWidth], nScreenWidth);
            cout << '\n';
        }

        cout << flush;
        this_thread::sleep_for(chrono::milliseconds(400));

        for (auto& v : vLines) {
            for (int px = 1; px < nFieldWidth - 1; px++) {
                for (int py = v; py > 0; py--) {
                    pField[py * nFieldWidth + px] =
                        pField[(py - 1) * nFieldWidth + px];
                }

                pField[px] = 0;
            }
        }

        vLines.clear();
    }
}

void display_frame(const vector<char>& screen) {
    cout << "\x1b[H";

    for (int y = 0; y < nScreenHeight; y++) {
        cout.write(&screen[y * nScreenWidth], nScreenWidth);
        cout << '\n';
    }

    std::cout.write(pDisplayBuffer, nDisplayBufferPos);
    nDisplayBufferPos = 0;

    cout << flush;
}

void clear_and_restore(int nScore) {
    clear_screen();
    cout << "Game Over!! Score: " << nScore << '\n';
    cout << "Press Enter to exit..." << flush;

    restore_terminal();
    cleanup();

    string dummy;
    getline(cin, dummy);
}

void handle_sigint(int sig) {
    (void)sig;
    printf("\033[0m\033[?25h");
    fflush(stdout);
    exit(0);
}


int main() {
    signal(SIGINT, handle_sigint);
    init();

    bool bKey[4] = {};
    int nCurrentPiece = rand() % 7;;
    int nCurrentRotation = 0;
    int nCurrentX = nFieldWidth / 2 - 2;
    int nCurrentY = 0;
    int nSpeed = 20;
    int nSpeedCount = 0;
    bool bForceDown = false;
    bool bRotateHold = true;
    int nPieceCount = 0;
    int nScore = 0;
    vector<int> vLines;
    bool bGameOver = false;

    // The original program used a Windows console screen buffer.
    // We instead render a normal terminal frame using ANSI escape sequences.
    vector<char> screen(nScreenWidth * nScreenHeight, ' ');

    while (!bGameOver) {
        rhythm(nSpeedCount, nSpeed, bForceDown);
        handle_key_input(bKey, sizeof(bKey), bGameOver);

        if (bGameOver) {
            break;
        }

        // Game Logic
        handle_player_movement(bKey, nCurrentX, nCurrentY, nCurrentPiece, nCurrentRotation);
        rotate_piece(bKey, nCurrentRotation, nCurrentPiece, nCurrentX, nCurrentY, bRotateHold);
        piece_down(bForceDown, nSpeedCount, nPieceCount, nSpeed, nScore, vLines, bGameOver,
                  nCurrentY, nCurrentPiece, nCurrentRotation, nCurrentX);
        display(screen, nCurrentX, nCurrentY, nCurrentPiece, nCurrentRotation, nScore);
        handle_line_completion(vLines, screen);
        display_frame(screen);
    }

    clear_and_restore(nScore);
    return 0;
}
