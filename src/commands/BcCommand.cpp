#include "commands/BcCommand.hpp"
#include "Commands.hpp"
#include "TuiScreen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <windows.h>

#include <cctype>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace commands {

namespace {

// ---------------------------------------------------------------------------
// Tiny recursive-descent expression parser.
// Grammar:
//   expression = term { ('+' | '-') term }
//   term       = factor { ('*' | '/') factor }
//   factor     = number | '(' expression ')'
// ---------------------------------------------------------------------------
class Parser {
public:
    explicit Parser(const std::string& text) : m_text{text}, m_pos{0} {}

    double parse()
    {
        double value = parseExpression();
        skipSpaces();
        if (m_pos != m_text.size()) {
            throw std::runtime_error("unexpected character");
        }
        return value;
    }

private:
    void skipSpaces()
    {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) {
            ++m_pos;
        }
    }

    double parseExpression()
    {
        double value = parseTerm();
        while (true) {
            skipSpaces();
            if (m_pos >= m_text.size()) break;
            char op = m_text[m_pos];
            if (op != '+' && op != '-') break;
            ++m_pos;
            double rhs = parseTerm();
            if (op == '+') value += rhs;
            else value -= rhs;
        }
        return value;
    }

    double parseTerm()
    {
        double value = parseFactor();
        while (true) {
            skipSpaces();
            if (m_pos >= m_text.size()) break;
            char op = m_text[m_pos];
            if (op != '*' && op != '/') break;
            ++m_pos;
            double rhs = parseFactor();
            if (op == '*') {
                value *= rhs;
            } else {
                if (rhs == 0.0) throw std::runtime_error("division by zero");
                value /= rhs;
            }
        }
        return value;
    }

    double parseFactor()
    {
        skipSpaces();
        if (m_pos >= m_text.size()) {
            throw std::runtime_error("expected factor");
        }

        if (m_text[m_pos] == '(') {
            ++m_pos;
            double value = parseExpression();
            skipSpaces();
            if (m_pos >= m_text.size() || m_text[m_pos] != ')') {
                throw std::runtime_error("missing closing parenthesis");
            }
            ++m_pos;
            return value;
        }

        return parseNumber();
    }

    double parseNumber()
    {
        skipSpaces();
        std::size_t start = m_pos;
        bool dotSeen = false;
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos];
            if (std::isdigit(static_cast<unsigned char>(c))) {
                ++m_pos;
            } else if (c == '.' && !dotSeen) {
                dotSeen = true;
                ++m_pos;
            } else {
                break;
            }
        }

        if (start == m_pos) {
            throw std::runtime_error("expected number");
        }

        std::string token = m_text.substr(start, m_pos - start);
        std::istringstream iss(token);
        double value = 0.0;
        iss >> value;
        return value;
    }

    const std::string& m_text;
    std::size_t m_pos;
};

static std::string formatResult(double value)
{
    // Avoid scientific notation for simple integer values.
    if (std::floor(value) == value && !std::isinf(value) && !std::isnan(value)) {
        std::ostringstream oss;
        oss << static_cast<long long>(value);
        return oss.str();
    }

    std::ostringstream oss;
    oss << value;
    return oss.str();
}

static bool isOperator(char c)
{
    return c == '+' || c == '-' || c == '*' || c == '/';
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Calculator command
// ---------------------------------------------------------------------------
void bc(const Args& /*args*/, std::ostream& out, std::istream& /*in*/, std::ostream& /*err*/)
{
    using namespace ftxui;

    std::string expression;
    std::string display = "0";
    bool showingResult = true;

    auto evaluate = [&] {
        if (expression.empty()) {
            display = "0";
            showingResult = true;
            return;
        }
        try {
            Parser parser(expression);
            double result = parser.parse();
            if (!std::isfinite(result)) {
                display = "Error";
            } else {
                display = formatResult(result);
            }
            expression = display;
        } catch (const std::exception&) {
            display = "Error";
            expression.clear();
        }
        showingResult = true;
    };

    auto append = [&](const std::string& token) {
        if (showingResult && !isOperator(token[0])) {
            // Start a fresh expression after a result unless the user picks an
            // operator, which should continue from the previous result.
            expression.clear();
            showingResult = false;
        } else if (showingResult && isOperator(token[0])) {
            showingResult = false;
        }

        // Avoid chaining multiple operators directly (replace the last operator).
        if (isOperator(token[0]) && !expression.empty() && isOperator(expression.back())) {
            expression.back() = token[0];
        } else {
            expression += token;
        }
        display = expression;
    };

    auto clear = [&] {
        expression.clear();
        display = "0";
        showingResult = true;
    };

    auto displayInput = Input(&display,
                           "display",
                           InputOption::Default());
    displayInput |= CatchEvent([](Event event) {
        // Ignore regular typing in the display; buttons handle input.
        if (event.is_character()) {
            return true;
        }
        return false;
    });

    auto makeButton = [&](const char* label, const std::string& token, Color bg) {
        return Button(label, [token, &append] { append(token); }, ButtonOption::Simple()) |
               size(HEIGHT, EQUAL, 3) | bgcolor(bg);
    };

    auto makeOpButton = [&](const char* label, const std::string& token) {
        return makeButton(label, token, Color::GrayDark);
    };

    auto makeDigitButton = [&](const char* label, const std::string& token) {
        return makeButton(label, token, Color::Blue);
    };

    auto buttonGrid = Container::Vertical({
        Container::Horizontal({
            makeDigitButton("7", "7"),
            makeDigitButton("8", "8"),
            makeDigitButton("9", "9"),
            makeOpButton("/", "/"),
        }),
        Container::Horizontal({
            makeDigitButton("4", "4"),
            makeDigitButton("5", "5"),
            makeDigitButton("6", "6"),
            makeOpButton("*", "*"),
        }),
        Container::Horizontal({
            makeDigitButton("1", "1"),
            makeDigitButton("2", "2"),
            makeDigitButton("3", "3"),
            makeOpButton("-", "-"),
        }),
        Container::Horizontal({
            makeDigitButton("0", "0"),
            makeButton(".", ".", Color::Blue),
            Button("C", clear, ButtonOption::Simple()) | size(HEIGHT, EQUAL, 3) | bgcolor(Color::Red),
            makeOpButton("+", "+"),
        }),
        Container::Horizontal({
            Button("=", evaluate, ButtonOption::Simple()) | flex | size(HEIGHT, EQUAL, 3) | bgcolor(Color::Green),
        }),
    });

    auto container = Container::Vertical({
        displayInput,
        buttonGrid,
    });

    auto renderer = Renderer(container, [&] {
        return window(
            text(" Calculator ") | bold | hcenter,
            vbox({
                hbox({
                    text("> ") | dim,
                    text(display),
                }) | border | size(HEIGHT, EQUAL, 3),
                buttonGrid->Render(),
                hbox({
                    text(" ESC = close ") | dim,
                    filler(),
                }),
            })
        ) | size(WIDTH, LESS_THAN, 50) | size(HEIGHT, LESS_THAN, 20) | center;
    });

    // Escape handling: pass a predicate to TuiScreen::run so it can terminate the
    // FTXUI event loop when the user presses Escape.
    auto shouldExit = [](const ftxui::Event& event) {
        return event == ftxui::Event::Escape;
    };

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD originalInputMode = 0;
    if (hInput != INVALID_HANDLE_VALUE) {
        GetConsoleMode(hInput, &originalInputMode);
    }

    {
        TuiScreen tuiScreen{hConsole, hInput, originalInputMode};
        tuiScreen.run(renderer, shouldExit);
    }

    // After closing, emit the last result on stdout so it can be piped or stored
    // in history.
    if (!display.empty() && display != "Error") {
        out << display << '\n';
    } else {
        out << "0\n";
    }
}

} // namespace commands
