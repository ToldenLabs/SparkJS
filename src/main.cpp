#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace spark {

// -----------------------------------------------------------------------------
// Diagnostics
// -----------------------------------------------------------------------------

struct Error : std::runtime_error {
    int line;
    int column;

    Error(int lineNumber, int columnNumber, std::string message)
        : std::runtime_error(std::move(message)),
          line(lineNumber),
          column(columnNumber) {}
};

[[noreturn]] static void fail(int line, int column, const std::string& message) {
    throw Error(line, column, message);
}

// -----------------------------------------------------------------------------
// Lexer
// -----------------------------------------------------------------------------

enum class TokenType {
    End,
    Identifier,
    Number,
    String,

    LeftParen, RightParen,
    LeftBrace, RightBrace,
    Comma, Semicolon,

    Plus, Minus, Star, Slash, Percent,
    Bang, Equal,
    Less, LessEqual, Greater, GreaterEqual,
    EqualEqual, BangEqual,
    StrictEqual, StrictNotEqual,
    AndAnd, OrOr,

    Let, Const, Var,
    If, Else, While,
    Function, Return,
    True, False,
    Null, Undefined
};

struct Token {
    TokenType type;
    std::string text;
    int line = 1;
    int column = 1;
};

class Lexer {
public:
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    std::vector<Token> scan() {
        std::vector<Token> tokens;
        tokens.reserve(source_.size() / 2 + 1);

        while (true) {
            skipWhitespaceAndComments();
            if (atEnd()) break;

            start_ = current_;
            startLine_ = line_;
            startColumn_ = column_;

            const char c = advance();
            if (isIdentifierStart(c)) {
                tokens.push_back(identifier());
            } else if (isDigit(c) || (c == '.' && isDigit(peek()))) {
                tokens.push_back(number());
            } else {
                tokens.push_back(symbol(c));
            }
        }

        tokens.push_back({TokenType::End, "", line_, column_});
        return tokens;
    }

private:
    std::string source_;
    std::size_t start_ = 0;
    std::size_t current_ = 0;
    int line_ = 1;
    int column_ = 1;
    int startLine_ = 1;
    int startColumn_ = 1;

    bool atEnd() const { return current_ >= source_.size(); }

    char peek() const {
        return atEnd() ? '\0' : source_[current_];
    }

    char peekNext() const {
        return current_ + 1 < source_.size() ? source_[current_ + 1] : '\0';
    }

    char advance() {
        const char c = source_[current_++];
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return c;
    }

    bool match(char expected) {
        if (atEnd() || source_[current_] != expected) return false;
        advance();
        return true;
    }

    static bool isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    static bool isHexDigit(char c) {
        return std::isdigit(static_cast<unsigned char>(c)) ||
               (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    static bool isIdentifierStart(char c) {
        return (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               c == '_' || c == '$';
    }

    static bool isIdentifierPart(char c) {
        return isIdentifierStart(c) || isDigit(c);
    }

    void skipWhitespaceAndComments() {
        for (;;) {
            switch (peek()) {
                case ' ':
                case '\r':
                case '\t':
                case '\v':
                case '\f':
                    advance();
                    break;

                case '\n':
                    advance();
                    break;

                case '/':
                    if (peekNext() == '/') {
                        advance();
                        advance();
                        while (!atEnd() && peek() != '\n') advance();
                    } else if (peekNext() == '*') {
                        const int commentLine = line_;
                        const int commentColumn = column_;
                        advance();
                        advance();

                        bool terminated = false;
                        while (!atEnd()) {
                            if (peek() == '*' && peekNext() == '/') {
                                advance();
                                advance();
                                terminated = true;
                                break;
                            }
                            advance();
                        }

                        if (!terminated) {
                            fail(commentLine, commentColumn,
                                 "Unterminated block comment");
                        }
                    } else {
                        return;
                    }
                    break;

                default:
                    return;
            }
        }
    }

    Token identifier() {
        while (isIdentifierPart(peek())) advance();

        const std::string text = source_.substr(start_, current_ - start_);

        static const std::unordered_map<std::string, TokenType> keywords = {
            {"let", TokenType::Let},
            {"const", TokenType::Const},
            {"var", TokenType::Var},
            {"if", TokenType::If},
            {"else", TokenType::Else},
            {"while", TokenType::While},
            {"function", TokenType::Function},
            {"return", TokenType::Return},
            {"true", TokenType::True},
            {"false", TokenType::False},
            {"null", TokenType::Null},
            {"undefined", TokenType::Undefined},
        };

        auto found = keywords.find(text);
        return {
            found == keywords.end() ? TokenType::Identifier : found->second,
            text,
            startLine_,
            startColumn_
        };
    }

    Token number() {
        // Hexadecimal, binary, and octal integer literals.
        if (source_[start_] == '0') {
            if (peek() == 'x' || peek() == 'X') {
                advance();
                const std::size_t digitsStart = current_;
                while (isHexDigit(peek())) advance();
                if (current_ == digitsStart) {
                    fail(startLine_, startColumn_, "Expected hexadecimal digits");
                }
                return {
                    TokenType::Number,
                    source_.substr(start_, current_ - start_),
                    startLine_,
                    startColumn_
                };
            }

            if (peek() == 'b' || peek() == 'B') {
                advance();
                const std::size_t digitsStart = current_;
                while (peek() == '0' || peek() == '1') advance();
                if (current_ == digitsStart) {
                    fail(startLine_, startColumn_, "Expected binary digits");
                }
                return {
                    TokenType::Number,
                    source_.substr(start_, current_ - start_),
                    startLine_,
                    startColumn_
                };
            }

            if (peek() == 'o' || peek() == 'O') {
                advance();
                const std::size_t digitsStart = current_;
                while (peek() >= '0' && peek() <= '7') advance();
                if (current_ == digitsStart) {
                    fail(startLine_, startColumn_, "Expected octal digits");
                }
                return {
                    TokenType::Number,
                    source_.substr(start_, current_ - start_),
                    startLine_,
                    startColumn_
                };
            }
        }

        if (source_[start_] == '.') {
            advance();
        } else {
            while (isDigit(peek())) advance();
            if (peek() == '.') {
                advance();
            }
        }

        while (isDigit(peek())) advance();

        if (peek() == 'e' || peek() == 'E') {
            const std::size_t exponentStart = current_;
            advance();
            if (peek() == '+' || peek() == '-') advance();

            if (!isDigit(peek())) {
                current_ = exponentStart;
                // Restore line/column. Exponents are ASCII and on one line.
                column_ -= static_cast<int>(current_ - exponentStart);
            } else {
                while (isDigit(peek())) advance();
            }
        }

        return {
            TokenType::Number,
            source_.substr(start_, current_ - start_),
            startLine_,
            startColumn_
        };
    }

    Token string(char quote) {
        std::string result;

        while (!atEnd() && peek() != quote) {
            const int escapeLine = line_;
            const int escapeColumn = column_;
            const char c = advance();

            if (c == '\n') {
                fail(startLine_, startColumn_, "Unterminated string literal");
            }

            if (c != '\\') {
                result += c;
                continue;
            }

            if (atEnd()) {
                fail(escapeLine, escapeColumn, "Unterminated escape sequence");
            }

            const char escaped = advance();
            switch (escaped) {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'v': result += '\v'; break;
                case '0': result += '\0'; break;
                case '\\': result += '\\'; break;
                case '\'': result += '\''; break;
                case '"': result += '"'; break;

                case '\n':
                    // Line continuation.
                    break;

                case 'x': {
                    if (!isHexDigit(peek()) || !isHexDigit(peekNext())) {
                        fail(escapeLine, escapeColumn,
                             "Expected two hexadecimal digits after \\x");
                    }
                    const char a = advance();
                    const char b = advance();
                    const auto hex = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        return c - 'A' + 10;
                    };
                    result += static_cast<char>((hex(a) << 4) | hex(b));
                    break;
                }

                default:
                    fail(escapeLine, escapeColumn,
                         std::string("Unknown string escape \\") + escaped);
            }
        }

        if (atEnd()) {
            fail(startLine_, startColumn_, "Unterminated string literal");
        }

        advance(); // closing quote

        return {TokenType::String, std::move(result), startLine_, startColumn_};
    }

    Token symbol(char c) {
        switch (c) {
            case '(': return {TokenType::LeftParen, "(", startLine_, startColumn_};
            case ')': return {TokenType::RightParen, ")", startLine_, startColumn_};
            case '{': return {TokenType::LeftBrace, "{", startLine_, startColumn_};
            case '}': return {TokenType::RightBrace, "}", startLine_, startColumn_};
            case ',': return {TokenType::Comma, ",", startLine_, startColumn_};
            case ';': return {TokenType::Semicolon, ";", startLine_, startColumn_};

            case '+': return {TokenType::Plus, "+", startLine_, startColumn_};
            case '-': return {TokenType::Minus, "-", startLine_, startColumn_};
            case '*': return {TokenType::Star, "*", startLine_, startColumn_};
            case '/': return {TokenType::Slash, "/", startLine_, startColumn_};
            case '%': return {TokenType::Percent, "%", startLine_, startColumn_};

            case '!':
                if (match('=')) {
                    if (match('=')) return {TokenType::StrictNotEqual, "!=", startLine_, startColumn_};
                    return {TokenType::BangEqual, "!=", startLine_, startColumn_};
                }
                return {TokenType::Bang, "!", startLine_, startColumn_};

            case '=':
                if (match('=')) {
                    if (match('=')) return {TokenType::StrictEqual, "===", startLine_, startColumn_};
                    return {TokenType::EqualEqual, "==", startLine_, startColumn_};
                }
                return {TokenType::Equal, "=", startLine_, startColumn_};

            case '<':
                return {
                    match('=') ? TokenType::LessEqual : TokenType::Less,
                    source_.substr(start_, current_ - start_),
                    startLine_, startColumn_
                };

            case '>':
                return {
                    match('=') ? TokenType::GreaterEqual : TokenType::Greater,
                    source_.substr(start_, current_ - start_),
                    startLine_, startColumn_
                };

            case '&':
                if (match('&')) return {TokenType::AndAnd, "&&", startLine_, startColumn_};
                break;

            case '|':
                if (match('|')) return {TokenType::OrOr, "||", startLine_, startColumn_};
                break;

            case '\'':
            case '"':
                return string(c);

            default:
                break;
        }

        fail(startLine_, startColumn_,
             std::string("Unexpected character '") + c + "'");
    }
};

// -----------------------------------------------------------------------------
// Runtime values
// -----------------------------------------------------------------------------

struct Null {};
struct FunctionRef { std::size_t index = 0; };

using Value = std::variant<
    std::monostate, // undefined
    Null,
    bool,
    double,
    std::string,
    FunctionRef
>;

static bool isUndefined(const Value& value) {
    return std::holds_alternative<std::monostate>(value);
}

static bool isNull(const Value& value) {
    return std::holds_alternative<Null>(value);
}

static std::string valueToString(const Value& value) {
    if (isUndefined(value)) return "undefined";
    if (isNull(value)) return "null";

    if (const auto* boolean = std::get_if<bool>(&value))
        return *boolean ? "true" : "false";

    if (const auto* number = std::get_if<double>(&value)) {
        if (std::isnan(*number)) return "NaN";
        if (*number == std::numeric_limits<double>::infinity()) return "Infinity";
        if (*number == -std::numeric_limits<double>::infinity()) return "-Infinity";

        std::ostringstream out;
        out << std::setprecision(15) << *number;
        return out.str();
    }

    if (const auto* string = std::get_if<std::string>(&value))
        return *string;

    return "[function]";
}

static bool truthy(const Value& value) {
    if (isUndefined(value) || isNull(value)) return false;

    if (const auto* boolean = std::get_if<bool>(&value))
        return *boolean;

    if (const auto* number = std::get_if<double>(&value))
        return *number != 0.0 && !std::isnan(*number);

    if (const auto* string = std::get_if<std::string>(&value))
        return !string->empty();

    return true;
}

static bool strictEqual(const Value& a, const Value& b) {
    if (a.index() != b.index()) return false;

    if (isUndefined(a) || isNull(a)) return true;
    if (const auto* x = std::get_if<bool>(&a)) return *x == std::get<bool>(b);
    if (const auto* x = std::get_if<double>(&a)) return *x == std::get<double>(b);
    if (const auto* x = std::get_if<std::string>(&a)) return *x == std::get<std::string>(b);
    if (const auto* x = std::get_if<FunctionRef>(&a))
        return x->index == std::get<FunctionRef>(b).index;

    return false;
}

static bool looseEqual(const Value& a, const Value& b) {
    if (strictEqual(a, b)) return true;

    if ((isNull(a) && isUndefined(b)) || (isUndefined(a) && isNull(b)))
        return true;

    if (const auto* boolean = std::get_if<bool>(&a))
        return looseEqual(Value{*boolean ? 1.0 : 0.0}, b);

    if (const auto* boolean = std::get_if<bool>(&b))
        return looseEqual(a, Value{*boolean ? 1.0 : 0.0});

    if (const auto* number = std::get_if<double>(&a)) {
        if (const auto* string = std::get_if<std::string>(&b)) {
            try {
                return *number == std::stod(*string);
            } catch (...) {
                return false;
            }
        }
    }

    if (const auto* string = std::get_if<std::string>(&a)) {
        if (const auto* number = std::get_if<double>(&b)) {
            try {
                return std::stod(*string) == *number;
            } catch (...) {
                return false;
            }
        }
    }

    return false;
}

// -----------------------------------------------------------------------------
// Bytecode
// -----------------------------------------------------------------------------

enum class Op {
    Constant,
    Undefined,
    NullValue,
    TrueValue,
    FalseValue,

    GetName,
    DefineName,
    DefineConst,
    SetName,

    Pop,

    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Negate,
    Positive,
    Not,

    Equal,
    NotEqual,
    StrictEqual,
    StrictNotEqual,

    Less,
    LessEqual,
    Greater,
    GreaterEqual,

    Jump,
    JumpIfFalse,
    JumpIfTrue,

    Call,
    Print,

    Return
};

struct Instruction {
    Op op;
    int operand = 0;
    int line = 0;
    int column = 0;
};

struct Chunk {
    std::vector<Instruction> code;
    std::vector<Value> constants;
    std::vector<std::string> names;
};

struct Function {
    std::string name;
    std::vector<std::string> parameters;
    Chunk chunk;
};

struct Program {
    Chunk main;
    std::vector<Function> functions;
};

// -----------------------------------------------------------------------------
// Compiler
// -----------------------------------------------------------------------------

class Compiler {
public:
    explicit Compiler(std::vector<Token> tokens)
        : tokens_(std::move(tokens)), chunk_(&program_.main) {}

    Program compile() {
        while (!check(TokenType::End)) declaration();

        emit(Op::Undefined);
        emit(Op::Return);

        return std::move(program_);
    }

private:
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    Program program_;
    Chunk* chunk_ = nullptr;
    int functionDepth_ = 0;

    const Token& peek() const { return tokens_[current_]; }

    const Token& previous() const {
        assert(current_ > 0);
        return tokens_[current_ - 1];
    }

    const Token& lookAhead(std::size_t distance) const {
        const std::size_t index =
            std::min(current_ + distance, tokens_.size() - 1);
        return tokens_[index];
    }

    bool check(TokenType type) const {
        return peek().type == type;
    }

    bool match(TokenType type) {
        if (!check(type)) return false;
        ++current_;
        return true;
    }

    const Token& consume(TokenType type, const std::string& message) {
        if (!check(type)) fail(peek().line, peek().column, message);
        return tokens_[current_++];
    }

    void optionalSemicolon() {
        match(TokenType::Semicolon);
    }

    int addConstant(Value value) {
        chunk_->constants.push_back(std::move(value));
        return static_cast<int>(chunk_->constants.size() - 1);
    }

    int addName(const std::string& name) {
        chunk_->names.push_back(name);
        return static_cast<int>(chunk_->names.size() - 1);
    }

    int emit(Op op, int operand = 0, int line = -1, int column = -1) {
        if (line < 0) {
            line = current_ == 0 ? 1 : previous().line;
            column = current_ == 0 ? 1 : previous().column;
        }

        chunk_->code.push_back({op, operand, line, column});
        return static_cast<int>(chunk_->code.size() - 1);
    }

    void patchJump(int location) {
        if (location < 0 ||
            static_cast<std::size_t>(location) >= chunk_->code.size()) {
            throw std::logic_error("Invalid jump patch location");
        }
        chunk_->code[location].operand =
            static_cast<int>(chunk_->code.size());
    }

    void declaration() {
        if (match(TokenType::Let) ||
            match(TokenType::Const) ||
            match(TokenType::Var)) {
            variableDeclaration();
        } else if (match(TokenType::Function)) {
            functionDeclaration();
        } else {
            statement();
        }
    }

    void variableDeclaration() {
        const Token declarationToken = previous();
        const bool immutable = declarationToken.type == TokenType::Const;

        const Token name =
            consume(TokenType::Identifier, "Expected variable name");

        if (match(TokenType::Equal)) {
            expression();
        } else {
            if (immutable) {
                fail(name.line, name.column,
                     "const declaration requires an initializer");
            }
            emit(Op::Undefined, 0, name.line, name.column);
        }

        emit(immutable ? Op::DefineConst : Op::DefineName,
             addName(name.text), name.line, name.column);

        optionalSemicolon();
    }

    void functionDeclaration() {
        const Token functionToken = previous();

        // Keep this limitation explicit rather than silently producing
        // incorrect closure semantics.
        if (functionDepth_ != 0) {
            fail(functionToken.line, functionToken.column,
                 "Nested functions are not supported yet");
        }

        const Token name =
            consume(TokenType::Identifier, "Expected function name");

        consume(TokenType::LeftParen,
                "Expected '(' after function name");

        Function function;
        function.name = name.text;

        if (!check(TokenType::RightParen)) {
            do {
                if (function.parameters.size() >= 255) {
                    fail(peek().line, peek().column,
                         "Too many parameters (maximum is 255)");
                }

                function.parameters.push_back(
                    consume(TokenType::Identifier,
                            "Expected parameter name").text);
            } while (match(TokenType::Comma));
        }

        consume(TokenType::RightParen,
                "Expected ')' after parameters");
        consume(TokenType::LeftBrace,
                "Expected '{' before function body");

        const std::size_t functionIndex = program_.functions.size();
        program_.functions.push_back(std::move(function));

        Chunk* enclosing = chunk_;
        chunk_ = &program_.functions[functionIndex].chunk;
        ++functionDepth_;

        while (!check(TokenType::RightBrace) && !check(TokenType::End))
            declaration();

        consume(TokenType::RightBrace,
                "Expected '}' after function body");

        // A function with no explicit return produces undefined.
        emit(Op::Undefined);
        emit(Op::Return);

        --functionDepth_;
        chunk_ = enclosing;

        emit(Op::Constant,
             addConstant(FunctionRef{functionIndex}),
             name.line, name.column);
        emit(Op::DefineName,
             addName(name.text),
             name.line, name.column);
    }

    void statement() {
        if (match(TokenType::If)) {
            ifStatement();
        } else if (match(TokenType::While)) {
            whileStatement();
        } else if (match(TokenType::Return)) {
            returnStatement();
        } else if (match(TokenType::LeftBrace)) {
            block();
        } else {
            expressionStatement();
        }
    }

    void block() {
        while (!check(TokenType::RightBrace) && !check(TokenType::End))
            declaration();

        consume(TokenType::RightBrace, "Expected '}' after block");
    }

    void ifStatement() {
        const Token ifToken = previous();

        consume(TokenType::LeftParen,
                "Expected '(' after if");
        expression();
        consume(TokenType::RightParen,
                "Expected ')' after condition");

        const int falseJump = emit(Op::JumpIfFalse, 0,
                                    ifToken.line, ifToken.column);
        emit(Op::Pop);

        statement();

        const int endJump = emit(Op::Jump, 0,
                                 ifToken.line, ifToken.column);
        patchJump(falseJump);

        emit(Op::Pop);

        if (match(TokenType::Else))
            statement();

        patchJump(endJump);
    }

    void whileStatement() {
        const Token whileToken = previous();
        const int loopStart = static_cast<int>(chunk_->code.size());

        consume(TokenType::LeftParen,
                "Expected '(' after while");
        expression();
        consume(TokenType::RightParen,
                "Expected ')' after condition");

        const int exitJump = emit(Op::JumpIfFalse, 0,
                                  whileToken.line, whileToken.column);
        emit(Op::Pop);

        statement();

        emit(Op::Jump, loopStart,
             whileToken.line, whileToken.column);

        patchJump(exitJump);
        emit(Op::Pop);
    }

    void returnStatement() {
        if (functionDepth_ == 0) {
            fail(previous().line, previous().column,
                 "return is only valid inside a function");
        }

        if (check(TokenType::Semicolon) ||
            check(TokenType::RightBrace) ||
            check(TokenType::End)) {
            emit(Op::Undefined);
        } else {
            expression();
        }

        optionalSemicolon();
        emit(Op::Return);
    }

    void expressionStatement() {
        expression();
        optionalSemicolon();
        emit(Op::Pop);
    }

    void expression() {
        assignment();
    }

    void assignment() {
        if (check(TokenType::Identifier) &&
            lookAhead(1).type == TokenType::Equal) {

            const Token name = tokens_[current_++];
            ++current_; // '='

            assignment();
            emit(Op::SetName, addName(name.text),
                 name.line, name.column);
            return;
        }

        logicalOr();
    }

    void logicalOr() {
        logicalAnd();

        while (match(TokenType::OrOr)) {
            const Token op = previous();

            // Preserve the left operand if truthy; otherwise discard it
            // and evaluate the right operand.
            const int evaluateRight = emit(Op::JumpIfTrue, 0,
                                           op.line, op.column);
            emit(Op::Pop);
            logicalAnd();
            patchJump(evaluateRight);
        }
    }

    void logicalAnd() {
        equality();

        while (match(TokenType::AndAnd)) {
            const Token op = previous();

            const int end = emit(Op::JumpIfFalse, 0,
                                 op.line, op.column);
            emit(Op::Pop);
            equality();
            patchJump(end);
        }
    }

    void equality() {
        comparison();

        while (match(TokenType::EqualEqual) ||
               match(TokenType::BangEqual) ||
               match(TokenType::StrictEqual) ||
               match(TokenType::StrictNotEqual)) {

            const TokenType op = previous().type;
            const Token token = previous();

            comparison();

            switch (op) {
                case TokenType::EqualEqual:
                    emit(Op::Equal, 0, token.line, token.column);
                    break;
                case TokenType::BangEqual:
                    emit(Op::NotEqual, 0, token.line, token.column);
                    break;
                case TokenType::StrictEqual:
                    emit(Op::StrictEqual, 0, token.line, token.column);
                    break;
                case TokenType::StrictNotEqual:
                    emit(Op::StrictNotEqual, 0, token.line, token.column);
                    break;
                default:
                    break;
            }
        }
    }

    void comparison() {
        term();

        while (match(TokenType::Less) ||
               match(TokenType::LessEqual) ||
               match(TokenType::Greater) ||
               match(TokenType::GreaterEqual)) {

            const TokenType op = previous().type;
            const Token token = previous();

            term();

            switch (op) {
                case TokenType::Less:
                    emit(Op::Less, 0, token.line, token.column);
                    break;
                case TokenType::LessEqual:
                    emit(Op::LessEqual, 0, token.line, token.column);
                    break;
                case TokenType::Greater:
                    emit(Op::Greater, 0, token.line, token.column);
                    break;
                case TokenType::GreaterEqual:
                    emit(Op::GreaterEqual, 0, token.line, token.column);
                    break;
                default:
                    break;
            }
        }
    }

    void term() {
        factor();

        while (match(TokenType::Plus) || match(TokenType::Minus)) {
            const TokenType op = previous().type;
            const Token token = previous();

            factor();
            emit(op == TokenType::Plus ? Op::Add : Op::Subtract,
                 0, token.line, token.column);
        }
    }

    void factor() {
        unary();

        while (match(TokenType::Star) ||
               match(TokenType::Slash) ||
               match(TokenType::Percent)) {

            const TokenType op = previous().type;
            const Token token = previous();

            unary();

            if (op == TokenType::Star)
                emit(Op::Multiply, 0, token.line, token.column);
            else if (op == TokenType::Slash)
                emit(Op::Divide, 0, token.line, token.column);
            else
                emit(Op::Modulo, 0, token.line, token.column);
        }
    }

    void unary() {
        if (match(TokenType::Bang)) {
            const Token token = previous();
            unary();
            emit(Op::Not, 0, token.line, token.column);
        } else if (match(TokenType::Minus)) {
            const Token token = previous();
            unary();
            emit(Op::Negate, 0, token.line, token.column);
        } else if (match(TokenType::Plus)) {
            const Token token = previous();
            unary();
            emit(Op::Positive, 0, token.line, token.column);
        } else {
            call();
        }
    }

    void call() {
        primary();

        while (match(TokenType::LeftParen)) {
            const Token callToken = previous();
            int argumentCount = 0;

            if (!check(TokenType::RightParen)) {
                do {
                    if (argumentCount >= 255) {
                        fail(peek().line, peek().column,
                             "Too many arguments (maximum is 255)");
                    }

                    expression();
                    ++argumentCount;
                } while (match(TokenType::Comma));
            }

            consume(TokenType::RightParen,
                    "Expected ')' after arguments");

            emit(Op::Call, argumentCount,
                 callToken.line, callToken.column);
        }
    }

    void primary() {
        if (match(TokenType::Number)) {
            emit(Op::Constant,
                 addConstant(parseNumber(previous().text)),
                 previous().line, previous().column);
        } else if (match(TokenType::String)) {
            emit(Op::Constant,
                 addConstant(previous().text),
                 previous().line, previous().column);
        } else if (match(TokenType::True)) {
            emit(Op::TrueValue);
        } else if (match(TokenType::False)) {
            emit(Op::FalseValue);
        } else if (match(TokenType::Null)) {
            emit(Op::NullValue);
        } else if (match(TokenType::Undefined)) {
            emit(Op::Undefined);
        } else if (match(TokenType::Identifier)) {
            const Token name = previous();

            // Keep print as a tiny builtin rather than polluting the language
            // with a special global object.
            if (name.text == "print" && check(TokenType::LeftParen)) {
                ++current_;

                int count = 0;
                if (!check(TokenType::RightParen)) {
                    do {
                        if (count >= 255) {
                            fail(peek().line, peek().column,
                                 "Too many print arguments");
                        }
                        expression();
                        ++count;
                    } while (match(TokenType::Comma));
                }

                consume(TokenType::RightParen,
                        "Expected ')' after print arguments");

                // Print is implemented through a normal call-shaped opcode
                // with a negative operand to avoid a separate AST path.
                emit(Op::Print, count, name.line, name.column);
            } else {
                emit(Op::GetName,
                     addName(name.text),
                     name.line, name.column);
            }
        } else if (match(TokenType::LeftParen)) {
            expression();
            consume(TokenType::RightParen,
                    "Expected ')' after expression");
        } else {
            fail(peek().line, peek().column,
                 "Expected expression");
        }
    }

    static double parseNumber(const std::string& text) {
        try {
            if (text.size() > 2 && text[0] == '0' &&
                (text[1] == 'x' || text[1] == 'X')) {
                return static_cast<double>(
                    std::stoull(text.substr(2), nullptr, 16));
            }

            if (text.size() > 2 && text[0] == '0' &&
                (text[1] == 'b' || text[1] == 'B')) {
                return static_cast<double>(
                    std::stoull(text.substr(2), nullptr, 2));
            }

            if (text.size() > 2 && text[0] == '0' &&
                (text[1] == 'o' || text[1] == 'O')) {
                return static_cast<double>(
                    std::stoull(text.substr(2), nullptr, 8));
            }

            return std::stod(text);
        } catch (...) {
            throw Error(1, 1, "Invalid numeric literal '" + text + "'");
        }
    }
};

// -----------------------------------------------------------------------------
// VM
// -----------------------------------------------------------------------------

class VM {
public:
    explicit VM(Program program)
        : program_(std::move(program)) {}

    Value run() {
        reset();

        frames_.push_back({
            &program_.main,
            0,
            {},
            0,
            true,
            "<main>"
        });

        while (!frames_.empty()) {
            if (frames_.size() > maxFrames_) {
                runtimeError(0, 0, "Maximum call stack size exceeded");
            }

            Frame& frame = frames_.back();

            if (frame.ip >= frame.chunk->code.size()) {
                runtimeError(0, 0, "Instruction pointer escaped bytecode");
            }

            const Instruction instruction = frame.chunk->code[frame.ip++];
            execute(instruction);
        }

        return lastResult_;
    }

private:
    struct Binding {
        Value value;
        bool mutableBinding = true;
    };

    struct Frame {
        const Chunk* chunk;
        std::size_t ip;
        std::unordered_map<std::string, Binding> locals;
        std::size_t stackBase;
        bool main;
        std::string functionName;
    };

    Program program_;
    std::vector<Value> stack_;
    std::vector<Frame> frames_;
    std::unordered_map<std::string, Binding> globals_;
    Value lastResult_;
    std::size_t maxFrames_ = 1024;

    void reset() {
        stack_.clear();
        frames_.clear();
        globals_.clear();
        lastResult_ = std::monostate{};
    }

    [[noreturn]] void runtimeError(
        int line, int column, const std::string& message) const {

        std::ostringstream out;

        if (!frames_.empty()) {
            out << message << " [";
            out << frames_.back().functionName;
            out << "]";
        } else {
            out << message;
        }

        throw Error(line, column, out.str());
    }

    Value pop(int line, int column) {
        if (stack_.empty())
            runtimeError(line, column, "Stack underflow");

        Value value = std::move(stack_.back());
        stack_.pop_back();
        return value;
    }

    Value& top(int line, int column) {
        if (stack_.empty())
            runtimeError(line, column, "Stack underflow");
        return stack_.back();
    }

    const Value& top(int line, int column) const {
        if (stack_.empty())
            runtimeError(line, column, "Stack underflow");
        return stack_.back();
    }

    double number(
        const Value& value,
        int line,
        int column,
        const char* operation) const {

        if (const auto* result = std::get_if<double>(&value))
            return *result;

        runtimeError(line, column,
                     std::string(operation) + " requires a number");
    }

    const std::string& name(
        const Frame& frame,
        int index,
        int line,
        int column) const {

        if (index < 0 ||
            static_cast<std::size_t>(index) >= frame.chunk->names.size()) {

            runtimeError(line, column, "Bad name index");
        }

        return frame.chunk->names[static_cast<std::size_t>(index)];
    }

    void binaryNumber(const Instruction& instruction, Op op) {
        Value right = pop(instruction.line, instruction.column);
        Value left  = pop(instruction.line, instruction.column);

        const double r =
            number(right, instruction.line, instruction.column, "Operator");
        const double l =
            number(left, instruction.line, instruction.column, "Operator");

        switch (op) {
            case Op::Subtract:
                stack_.push_back(l - r);
                break;

            case Op::Multiply:
                stack_.push_back(l * r);
                break;

            case Op::Divide:
                // JavaScript normally produces Infinity/NaN here rather than
                // throwing. Preserve IEEE-754 behavior for this lightweight
                // engine.
                stack_.push_back(l / r);
                break;

            case Op::Modulo:
                stack_.push_back(std::fmod(l, r));
                break;

            case Op::Less:
                stack_.push_back(l < r);
                break;

            case Op::LessEqual:
                stack_.push_back(l <= r);
                break;

            case Op::Greater:
                stack_.push_back(l > r);
                break;

            case Op::GreaterEqual:
                stack_.push_back(l >= r);
                break;

            default:
                runtimeError(instruction.line, instruction.column,
                             "Invalid numeric opcode");
        }
    }

    void execute(const Instruction& in) {
        Frame& frame = frames_.back();

        switch (in.op) {
            case Op::Constant: {
                if (in.operand < 0 ||
                    static_cast<std::size_t>(in.operand) >=
                        frame.chunk->constants.size()) {

                    runtimeError(in.line, in.column,
                                 "Bad constant index");
                }

                stack_.push_back(
                    frame.chunk->constants[
                        static_cast<std::size_t>(in.operand)]);
                break;
            }

            case Op::Undefined:
                stack_.emplace_back(std::monostate{});
                break;

            case Op::NullValue:
                stack_.emplace_back(Null{});
                break;

            case Op::TrueValue:
                stack_.emplace_back(true);
                break;

            case Op::FalseValue:
                stack_.emplace_back(false);
                break;

            case Op::GetName: {
                const std::string& key =
                    name(frame, in.operand, in.line, in.column);

                auto local = frame.locals.find(key);
                if (local != frame.locals.end()) {
                    stack_.push_back(local->second.value);
                    break;
                }

                auto global = globals_.find(key);
                if (global == globals_.end()) {
                    runtimeError(in.line, in.column,
                                 "Undefined variable '" + key + "'");
                }

                stack_.push_back(global->second.value);
                break;
            }

            case Op::DefineName:
            case Op::DefineConst: {
                const std::string& key =
                    name(frame, in.operand, in.line, in.column);

                Value value = pop(in.line, in.column);
                const bool mutableBinding =
                    in.op == Op::DefineName;

                auto& table = frame.main ? globals_ : frame.locals;

                // Re-declaration is intentionally rejected. This avoids a
                // particularly nasty class of silent bytecode/runtime bugs.
                if (table.find(key) != table.end()) {
                    runtimeError(in.line, in.column,
                                 "Identifier '" + key +
                                 "' has already been declared");
                }

                table.emplace(
                    key,
                    Binding{std::move(value), mutableBinding});
                break;
            }

            case Op::SetName: {
                const std::string& key =
                    name(frame, in.operand, in.line, in.column);

                auto local = frame.locals.find(key);
                if (local != frame.locals.end()) {
                    if (!local->second.mutableBinding) {
                        runtimeError(in.line, in.column,
                                     "Assignment to constant '" + key + "'");
                    }

                    local->second.value = top(in.line, in.column);
                    break;
                }

                auto global = globals_.find(key);
                if (global == globals_.end()) {
                    runtimeError(in.line, in.column,
                                 "Assignment to undefined variable '" + key + "'");
                }

                if (!global->second.mutableBinding) {
                    runtimeError(in.line, in.column,
                                 "Assignment to constant '" + key + "'");
                }

                global->second.value = top(in.line, in.column);
                break;
            }

            case Op::Pop:
                (void)pop(in.line, in.column);
                break;

            case Op::Add: {
                Value right = pop(in.line, in.column);
                Value left  = pop(in.line, in.column);

                if (std::holds_alternative<std::string>(left) ||
                    std::holds_alternative<std::string>(right)) {
                    stack_.push_back(
                        valueToString(left) + valueToString(right));
                } else {
                    const double l =
                        number(left, in.line, in.column, "+");
                    const double r =
                        number(right, in.line, in.column, "+");
                    stack_.push_back(l + r);
                }
                break;
            }

            case Op::Subtract:
            case Op::Multiply:
            case Op::Divide:
            case Op::Modulo:
            case Op::Less:
            case Op::LessEqual:
            case Op::Greater:
            case Op::GreaterEqual:
                binaryNumber(in, in.op);
                break;

            case Op::Negate: {
                const double value =
                    number(pop(in.line, in.column),
                           in.line, in.column, "Unary -");
                stack_.push_back(-value);
                break;
            }

            case Op::Positive: {
                const double value =
                    number(pop(in.line, in.column),
                           in.line, in.column, "Unary +");
                stack_.push_back(value);
                break;
            }

            case Op::Not:
                stack_.push_back(
                    !truthy(pop(in.line, in.column)));
                break;

            case Op::Equal: {
                Value b = pop(in.line, in.column);
                Value a = pop(in.line, in.column);
                stack_.push_back(looseEqual(a, b));
                break;
            }

            case Op::NotEqual: {
                Value b = pop(in.line, in.column);
                Value a = pop(in.line, in.column);
                stack_.push_back(!looseEqual(a, b));
                break;
            }

            case Op::StrictEqual: {
                Value b = pop(in.line, in.column);
                Value a = pop(in.line, in.column);
                stack_.push_back(strictEqual(a, b));
                break;
            }

            case Op::StrictNotEqual: {
                Value b = pop(in.line, in.column);
                Value a = pop(in.line, in.column);
                stack_.push_back(!strictEqual(a, b));
                break;
            }

            case Op::Jump:
                setInstructionPointer(frame, in.operand, in);
                break;

            case Op::JumpIfFalse:
                if (!truthy(top(in.line, in.column)))
                    setInstructionPointer(frame, in.operand, in);
                break;

            case Op::JumpIfTrue:
                if (truthy(top(in.line, in.column)))
                    setInstructionPointer(frame, in.operand, in);
                break;

            case Op::Call:
                callFunction(in);
                break;

            case Op::Print: {
                if (in.operand < 0 ||
                    stack_.size() <
                        static_cast<std::size_t>(in.operand)) {

                    runtimeError(in.line, in.column,
                                 "Bad print call");
                }

                const std::size_t count =
                    static_cast<std::size_t>(in.operand);
                const std::size_t start = stack_.size() - count;

                for (std::size_t i = start; i < stack_.size(); ++i) {
                    if (i != start) std::cout << ' ';
                    std::cout << valueToString(stack_[i]);
                }

                std::cout << '\n';
                stack_.resize(start);
                stack_.emplace_back(std::monostate{});
                break;
            }

            case Op::Return:
                returnFromFunction(in);
                break;
        }
    }

    void setInstructionPointer(
        Frame& frame,
        int target,
        const Instruction& instruction) {

        if (target < 0 ||
            static_cast<std::size_t>(target) > frame.chunk->code.size()) {

            runtimeError(instruction.line, instruction.column,
                         "Invalid jump target");
        }

        frame.ip = static_cast<std::size_t>(target);
    }

    void callFunction(const Instruction& in) {
        if (in.operand < 0 ||
            stack_.size() <
                static_cast<std::size_t>(in.operand + 1)) {

            runtimeError(in.line, in.column,
                         "Bad function call");
        }

        const std::size_t calleeIndex =
            stack_.size() -
            static_cast<std::size_t>(in.operand) - 1;

        auto* reference =
            std::get_if<FunctionRef>(&stack_[calleeIndex]);

        if (!reference ||
            reference->index >= program_.functions.size()) {

            runtimeError(in.line, in.column,
                         "Value is not callable");
        }

        const Function& function =
            program_.functions[reference->index];

        if (function.parameters.size() !=
            static_cast<std::size_t>(in.operand)) {

            runtimeError(
                in.line, in.column,
                "Function '" + function.name +
                "' expected " +
                std::to_string(function.parameters.size()) +
                " arguments but received " +
                std::to_string(in.operand));
        }

        Frame next{
            &function.chunk,
            0,
            {},
            calleeIndex,
            false,
            function.name
        };

        for (int i = 0; i < in.operand; ++i) {
            next.locals.emplace(
                function.parameters[
                    static_cast<std::size_t>(i)],
                Binding{
                    stack_[calleeIndex + 1 +
                           static_cast<std::size_t>(i)],
                    true
                });
        }

        // Remove callee and arguments. The calleeIndex is the base where the
        // function result will eventually be restored.
        stack_.resize(calleeIndex);
        frames_.push_back(std::move(next));
    }

    void returnFromFunction(const Instruction& in) {
        Value result = pop(in.line, in.column);

        const Frame& frame = frames_.back();
        const std::size_t base = frame.stackBase;
        const bool main = frame.main;

        frames_.pop_back();

        if (base > stack_.size()) {
            runtimeError(in.line, in.column,
                         "Invalid frame stack base");
        }

        stack_.resize(base);

        if (main) {
            lastResult_ = std::move(result);
        } else {
            stack_.push_back(std::move(result));
        }
    }
};

// -----------------------------------------------------------------------------
// Debugging / CLI
// -----------------------------------------------------------------------------

static const char* tokenName(TokenType type) {
    switch (type) {
        case TokenType::End: return "END";
        case TokenType::Identifier: return "IDENTIFIER";
        case TokenType::Number: return "NUMBER";
        case TokenType::String: return "STRING";
        case TokenType::LeftParen: return "LEFT_PAREN";
        case TokenType::RightParen: return "RIGHT_PAREN";
        case TokenType::LeftBrace: return "LEFT_BRACE";
        case TokenType::RightBrace: return "RIGHT_BRACE";
        case TokenType::Comma: return "COMMA";
        case TokenType::Semicolon: return "SEMICOLON";
        case TokenType::Plus: return "PLUS";
        case TokenType::Minus: return "MINUS";
        case TokenType::Star: return "STAR";
        case TokenType::Slash: return "SLASH";
        case TokenType::Percent: return "PERCENT";
        case TokenType::Bang: return "BANG";
        case TokenType::Equal: return "EQUAL";
        case TokenType::Less: return "LESS";
        case TokenType::LessEqual: return "LESS_EQUAL";
        case TokenType::Greater: return "GREATER";
        case TokenType::GreaterEqual: return "GREATER_EQUAL";
        case TokenType::EqualEqual: return "EQUAL_EQUAL";
        case TokenType::BangEqual: return "BANG_EQUAL";
        case TokenType::StrictEqual: return "STRICT_EQUAL";
        case TokenType::StrictNotEqual: return "STRICT_NOT_EQUAL";
        case TokenType::AndAnd: return "AND_AND";
        case TokenType::OrOr: return "OR_OR";
        case TokenType::Let: return "LET";
        case TokenType::Const: return "CONST";
        case TokenType::Var: return "VAR";
        case TokenType::If: return "IF";
        case TokenType::Else: return "ELSE";
        case TokenType::While: return "WHILE";
        case TokenType::Function: return "FUNCTION";
        case TokenType::Return: return "RETURN";
        case TokenType::True: return "TRUE";
        case TokenType::False: return "FALSE";
        case TokenType::Null: return "NULL";
        case TokenType::Undefined: return "UNDEFINED";
    }

    return "?";
}

static const char* opName(Op op) {
    switch (op) {
        case Op::Constant: return "CONSTANT";
        case Op::Undefined: return "UNDEFINED";
        case Op::NullValue: return "NULL";
        case Op::TrueValue: return "TRUE";
        case Op::FalseValue: return "FALSE";
        case Op::GetName: return "GET_NAME";
        case Op::DefineName: return "DEFINE_NAME";
        case Op::DefineConst: return "DEFINE_CONST";
        case Op::SetName: return "SET_NAME";
        case Op::Pop: return "POP";
        case Op::Add: return "ADD";
        case Op::Subtract: return "SUBTRACT";
        case Op::Multiply: return "MULTIPLY";
        case Op::Divide: return "DIVIDE";
        case Op::Modulo: return "MODULO";
        case Op::Negate: return "NEGATE";
        case Op::Positive: return "POSITIVE";
        case Op::Not: return "NOT";
        case Op::Equal: return "EQUAL";
        case Op::NotEqual: return "NOT_EQUAL";
        case Op::StrictEqual: return "STRICT_EQUAL";
        case Op::StrictNotEqual: return "STRICT_NOT_EQUAL";
        case Op::Less: return "LESS";
        case Op::LessEqual: return "LESS_EQUAL";
        case Op::Greater: return "GREATER";
        case Op::GreaterEqual: return "GREATER_EQUAL";
        case Op::Jump: return "JUMP";
        case Op::JumpIfFalse: return "JUMP_IF_FALSE";
        case Op::JumpIfTrue: return "JUMP_IF_TRUE";
        case Op::Call: return "CALL";
        case Op::Print: return "PRINT";
        case Op::Return: return "RETURN";
    }

    return "?";
}

static void dumpChunk(const Chunk& chunk, const std::string& label) {
    std::cout << "== " << label << " ==\n";

    for (std::size_t i = 0; i < chunk.code.size(); ++i) {
        const Instruction& in = chunk.code[i];

        std::cout
            << std::setw(5) << i
            << "  "
            << std::setw(4) << in.line
            << ":"
            << std::setw(3) << in.column
            << "  "
            << std::left
            << std::setw(18)
            << opName(in.op)
            << std::right;

        if (in.operand != 0 ||
            in.op == Op::Constant ||
            in.op == Op::GetName ||
            in.op == Op::DefineName ||
            in.op == Op::DefineConst ||
            in.op == Op::SetName ||
            in.op == Op::Call ||
            in.op == Op::Print) {

            std::cout << in.operand;
        }

        std::cout << '\n';
    }
}

static std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(
            "Could not open '" + path + "'");
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

static void printTokens(const std::vector<Token>& tokens) {
    for (const Token& token : tokens) {
        std::cout
            << token.line << ":"
            << token.column << "\t"
            << tokenName(token.type) << "\t"
            << token.text << '\n';
    }
}

static void executeSource(
    const std::string& source,
    bool showTokens,
    bool showBytecode) {

    std::vector<Token> tokens = Lexer(source).scan();

    if (showTokens)
        printTokens(tokens);

    Program program =
        Compiler(std::move(tokens)).compile();

    if (showBytecode) {
        dumpChunk(program.main, "main");

        for (const Function& function : program.functions)
            dumpChunk(function.chunk, function.name);
    }

    VM(std::move(program)).run();
}

} // namespace spark

int main(int argc, char** argv) {
    bool showTokens = false;
    bool showBytecode = false;
    std::optional<std::string> filename;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];

        if (argument == "--tokens") {
            showTokens = true;
        } else if (argument == "--bytecode") {
            showBytecode = true;
        } else if (argument == "--help" || argument == "-h") {
            std::cout
                << "SparkJS 0.2\n"
                << "Usage: sparkjs [options] [file.js]\n\n"
                << "Options:\n"
                << "  --tokens      Dump lexer tokens\n"
                << "  --bytecode    Dump generated bytecode\n"
                << "  --help, -h    Show this help\n";
            return 0;
        } else if (!filename) {
            filename = argument;
        } else {
            std::cerr
                << "Unexpected argument: "
                << argument << '\n';
            return 64;
        }
    }

    try {
        if (filename) {
            spark::executeSource(
                spark::readFile(*filename),
                showTokens,
                showBytecode);
        } else {
            std::cout
                << "SparkJS 0.2 REPL "
                << "(Ctrl-D to exit)\n";

            std::string line;
            while (std::cout << "> " &&
                   std::getline(std::cin, line)) {

                try {
                    spark::executeSource(
                        line,
                        showTokens,
                        showBytecode);
                } catch (const spark::Error& error) {
                    std::cerr
                        << "["
                        << error.line
                        << ":"
                        << error.column
                        << "] "
                        << error.what()
                        << '\n';
                }
            }

            std::cout << '\n';
        }
    } catch (const spark::Error& error) {
        std::cerr
            << "["
            << error.line
            << ":"
            << error.column
            << "] "
            << error.what()
            << '\n';
        return 65;
    } catch (const std::exception& error) {
        std::cerr
            << "error: "
            << error.what()
            << '\n';
        return 66;
    }

    return 0;
}
