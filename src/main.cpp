#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace spark {

struct Error : std::runtime_error {
  int line;
  Error(int lineNumber, const std::string& message)
      : std::runtime_error(message), line(lineNumber) {}
};

enum class TokenType {
  End, Identifier, Number, String,
  LeftParen, RightParen, LeftBrace, RightBrace, Comma, Semicolon,
  Plus, Minus, Star, Slash, Percent, Bang, Equal,
  Less, LessEqual, Greater, GreaterEqual, EqualEqual, BangEqual,
  AndAnd, OrOr,
  Let, Const, Var, If, Else, While, Function, Return,
  True, False, Null, Undefined
};

struct Token {
  TokenType type;
  std::string text;
  int line;
};

class Lexer {
 public:
  explicit Lexer(std::string source) : source_(std::move(source)) {}

  std::vector<Token> scan() {
    std::vector<Token> tokens;
    while (!atEnd()) {
      skipWhitespaceAndComments();
      if (atEnd()) break;
      start_ = current_;
      const int tokenLine = line_;
      const char c = advance();
      if (isAlpha(c)) {
        tokens.push_back(identifier(tokenLine));
      } else if (isDigit(c)) {
        tokens.push_back(number(tokenLine));
      } else {
        tokens.push_back(symbol(c, tokenLine));
      }
    }
    tokens.push_back({TokenType::End, "", line_});
    return tokens;
  }

 private:
  std::string source_;
  std::size_t start_ = 0;
  std::size_t current_ = 0;
  int line_ = 1;

  bool atEnd() const { return current_ >= source_.size(); }
  char peek() const { return atEnd() ? '\0' : source_[current_]; }
  char peekNext() const {
    return current_ + 1 >= source_.size() ? '\0' : source_[current_ + 1];
  }
  char advance() { return source_[current_++]; }
  bool match(char expected) {
    if (atEnd() || source_[current_] != expected) return false;
    ++current_;
    return true;
  }
  static bool isDigit(char c) { return c >= '0' && c <= '9'; }
  static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
  }
  static bool isAlphaNumeric(char c) { return isAlpha(c) || isDigit(c); }

  void skipWhitespaceAndComments() {
    for (;;) {
      switch (peek()) {
        case ' ': case '\r': case '\t': ++current_; break;
        case '\n': ++line_; ++current_; break;
        case '/':
          if (peekNext() == '/') {
            current_ += 2;
            while (peek() != '\n' && !atEnd()) ++current_;
          } else if (peekNext() == '*') {
            current_ += 2;
            while (!atEnd() && !(peek() == '*' && peekNext() == '/')) {
              if (peek() == '\n') ++line_;
              ++current_;
            }
            if (atEnd()) throw Error(line_, "Unterminated block comment");
            current_ += 2;
          } else {
            return;
          }
          break;
        default: return;
      }
    }
  }

  Token identifier(int tokenLine) {
    while (isAlphaNumeric(peek())) advance();
    std::string text = source_.substr(start_, current_ - start_);
    static const std::unordered_map<std::string, TokenType> keywords = {
      {"let", TokenType::Let}, {"const", TokenType::Const}, {"var", TokenType::Var},
      {"if", TokenType::If}, {"else", TokenType::Else}, {"while", TokenType::While},
      {"function", TokenType::Function}, {"return", TokenType::Return},
      {"true", TokenType::True}, {"false", TokenType::False}, {"null", TokenType::Null},
      {"undefined", TokenType::Undefined}
    };
    auto found = keywords.find(text);
    return {found == keywords.end() ? TokenType::Identifier : found->second, text, tokenLine};
  }

  Token number(int tokenLine) {
    while (isDigit(peek())) advance();
    if (peek() == '.' && isDigit(peekNext())) {
      advance();
      while (isDigit(peek())) advance();
    }
    if (peek() == 'e' || peek() == 'E') {
      const std::size_t exponentStart = current_;
      advance();
      if (peek() == '+' || peek() == '-') advance();
      if (!isDigit(peek())) current_ = exponentStart;
      else while (isDigit(peek())) advance();
    }
    return {TokenType::Number, source_.substr(start_, current_ - start_), tokenLine};
  }

  Token string(char quote, int tokenLine) {
    std::string result;
    while (!atEnd() && peek() != quote) {
      char c = advance();
      if (c == '\n') ++line_;
      if (c == '\\') {
        if (atEnd()) break;
        switch (advance()) {
          case 'n': result += '\n'; break;
          case 'r': result += '\r'; break;
          case 't': result += '\t'; break;
          case '\\': result += '\\'; break;
          case '\'': result += '\''; break;
          case '"': result += '"'; break;
          default: throw Error(line_, "Unknown string escape");
        }
      } else {
        result += c;
      }
    }
    if (atEnd()) throw Error(tokenLine, "Unterminated string");
    advance();
    return {TokenType::String, result, tokenLine};
  }

  Token symbol(char c, int tokenLine) {
    switch (c) {
      case '(': return {TokenType::LeftParen, "(", tokenLine};
      case ')': return {TokenType::RightParen, ")", tokenLine};
      case '{': return {TokenType::LeftBrace, "{", tokenLine};
      case '}': return {TokenType::RightBrace, "}", tokenLine};
      case ',': return {TokenType::Comma, ",", tokenLine};
      case ';': return {TokenType::Semicolon, ";", tokenLine};
      case '+': return {TokenType::Plus, "+", tokenLine};
      case '-': return {TokenType::Minus, "-", tokenLine};
      case '*': return {TokenType::Star, "*", tokenLine};
      case '/': return {TokenType::Slash, "/", tokenLine};
      case '%': return {TokenType::Percent, "%", tokenLine};
      case '!': return {match('=') ? TokenType::BangEqual : TokenType::Bang,
                        current_ - start_ == 2 ? "!=" : "!", tokenLine};
      case '=': return {match('=') ? TokenType::EqualEqual : TokenType::Equal,
                        current_ - start_ == 2 ? "==" : "=", tokenLine};
      case '<': return {match('=') ? TokenType::LessEqual : TokenType::Less,
                        current_ - start_ == 2 ? "<=" : "<", tokenLine};
      case '>': return {match('=') ? TokenType::GreaterEqual : TokenType::Greater,
                        current_ - start_ == 2 ? ">=" : ">", tokenLine};
      case '&': if (match('&')) return {TokenType::AndAnd, "&&", tokenLine}; break;
      case '|': if (match('|')) return {TokenType::OrOr, "||", tokenLine}; break;
      case '\'': case '"': return string(c, tokenLine);
    }
    throw Error(tokenLine, std::string("Unexpected character: ") + c);
  }
};

struct Null {};
struct FunctionRef { std::size_t index; };
using Value = std::variant<std::monostate, Null, bool, double, std::string, FunctionRef>;

static std::string valueToString(const Value& value) {
  if (std::holds_alternative<std::monostate>(value)) return "undefined";
  if (std::holds_alternative<Null>(value)) return "null";
  if (auto boolean = std::get_if<bool>(&value)) return *boolean ? "true" : "false";
  if (auto number = std::get_if<double>(&value)) {
    std::ostringstream out;
    out << std::setprecision(15) << *number;
    return out.str();
  }
  if (auto string = std::get_if<std::string>(&value)) return *string;
  return "[function]";
}

static bool truthy(const Value& value) {
  if (std::holds_alternative<std::monostate>(value) || std::holds_alternative<Null>(value)) return false;
  if (auto boolean = std::get_if<bool>(&value)) return *boolean;
  if (auto number = std::get_if<double>(&value)) return *number != 0.0 && !std::isnan(*number);
  if (auto string = std::get_if<std::string>(&value)) return !string->empty();
  return true;
}

enum class Op {
  Constant, Undefined, NullValue, TrueValue, FalseValue,
  GetName, DefineName, DefineConst, SetName, Pop,
  Add, Subtract, Multiply, Divide, Modulo, Negate, Not,
  Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
  And, Or, Jump, JumpIfFalse, Call, Print, Return
};

struct Instruction { Op op; int operand = 0; int line = 0; };
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
struct Program { Chunk main; std::vector<Function> functions; };

class Compiler {
 public:
  explicit Compiler(std::vector<Token> tokens) : tokens_(std::move(tokens)), chunk_(&program_.main) {}

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
  Chunk* chunk_;
  int functionDepth_ = 0;

  const Token& peek() const { return tokens_[current_]; }
  const Token& previous() const { return tokens_[current_ - 1]; }
  const Token& lookAhead(std::size_t distance) const {
    return tokens_[std::min(current_ + distance, tokens_.size() - 1)];
  }
  bool check(TokenType type) const { return peek().type == type; }
  bool match(TokenType type) {
    if (!check(type)) return false;
    ++current_;
    return true;
  }
  const Token& consume(TokenType type, const std::string& message) {
    if (!check(type)) throw Error(peek().line, message);
    return tokens_[current_++];
  }
  void optionalSemicolon() { match(TokenType::Semicolon); }

  int addConstant(Value value) {
    chunk_->constants.push_back(std::move(value));
    return static_cast<int>(chunk_->constants.size() - 1);
  }
  int addName(const std::string& name) {
    chunk_->names.push_back(name);
    return static_cast<int>(chunk_->names.size() - 1);
  }
  int emit(Op op, int operand = 0, int line = -1) {
    if (line < 0) line = current_ == 0 ? 1 : previous().line;
    chunk_->code.push_back({op, operand, line});
    return static_cast<int>(chunk_->code.size() - 1);
  }
  void patchJump(int location) { chunk_->code[location].operand = static_cast<int>(chunk_->code.size()); }

  void declaration() {
    if (match(TokenType::Let) || match(TokenType::Const) || match(TokenType::Var)) variableDeclaration();
    else if (match(TokenType::Function)) functionDeclaration();
    else statement();
  }

  void variableDeclaration() {
    const bool immutable = previous().type == TokenType::Const;
    Token name = consume(TokenType::Identifier, "Expected variable name");
    if (match(TokenType::Equal)) expression();
    else {
      if (immutable) throw Error(name.line, "const declaration requires an initializer");
      emit(Op::Undefined, 0, name.line);
    }
    emit(immutable ? Op::DefineConst : Op::DefineName, addName(name.text), name.line);
    optionalSemicolon();
  }

  void functionDeclaration() {
    if (functionDepth_ != 0) {
      throw Error(previous().line, "Nested functions and closures are not supported yet");
    }
    Token name = consume(TokenType::Identifier, "Expected function name");
    consume(TokenType::LeftParen, "Expected '(' after function name");

    Function function;
    function.name = name.text;
    if (!check(TokenType::RightParen)) {
      do {
        if (function.parameters.size() >= 255) throw Error(peek().line, "Too many parameters");
        function.parameters.push_back(consume(TokenType::Identifier, "Expected parameter name").text);
      } while (match(TokenType::Comma));
    }
    consume(TokenType::RightParen, "Expected ')' after parameters");
    consume(TokenType::LeftBrace, "Expected '{' before function body");

    const std::size_t functionIndex = program_.functions.size();
    program_.functions.push_back(std::move(function));
    Chunk* enclosing = chunk_;
    chunk_ = &program_.functions[functionIndex].chunk;
    ++functionDepth_;
    while (!check(TokenType::RightBrace) && !check(TokenType::End)) declaration();
    consume(TokenType::RightBrace, "Expected '}' after function body");
    emit(Op::Undefined);
    emit(Op::Return);
    --functionDepth_;
    chunk_ = enclosing;

    emit(Op::Constant, addConstant(FunctionRef{functionIndex}), name.line);
    emit(Op::DefineName, addName(name.text), name.line);
  }

  void statement() {
    if (match(TokenType::If)) ifStatement();
    else if (match(TokenType::While)) whileStatement();
    else if (match(TokenType::Return)) returnStatement();
    else if (match(TokenType::LeftBrace)) block();
    else expressionStatement();
  }

  void block() {
    while (!check(TokenType::RightBrace) && !check(TokenType::End)) declaration();
    consume(TokenType::RightBrace, "Expected '}' after block");
  }

  void ifStatement() {
    consume(TokenType::LeftParen, "Expected '(' after if");
    expression();
    consume(TokenType::RightParen, "Expected ')' after condition");
    int falseJump = emit(Op::JumpIfFalse, 0);
    emit(Op::Pop);
    statement();
    int endJump = emit(Op::Jump, 0);
    patchJump(falseJump);
    emit(Op::Pop);
    if (match(TokenType::Else)) statement();
    patchJump(endJump);
  }

  void whileStatement() {
    const int loopStart = static_cast<int>(chunk_->code.size());
    consume(TokenType::LeftParen, "Expected '(' after while");
    expression();
    consume(TokenType::RightParen, "Expected ')' after condition");
    int exitJump = emit(Op::JumpIfFalse, 0);
    emit(Op::Pop);
    statement();
    emit(Op::Jump, loopStart);
    patchJump(exitJump);
    emit(Op::Pop);
  }

  void returnStatement() {
    if (functionDepth_ == 0) throw Error(previous().line, "return is only valid inside a function");
    if (check(TokenType::Semicolon) || check(TokenType::RightBrace)) emit(Op::Undefined);
    else expression();
    optionalSemicolon();
    emit(Op::Return);
  }

  void expressionStatement() {
    expression();
    optionalSemicolon();
    emit(Op::Pop);
  }

  void expression() { assignment(); }
  void assignment() {
    if (check(TokenType::Identifier) && lookAhead(1).type == TokenType::Equal) {
      Token name = tokens_[current_++];
      ++current_;
      assignment();
      emit(Op::SetName, addName(name.text), name.line);
      return;
    }
    logicalOr();
  }
  void logicalOr() {
    logicalAnd();
    while (match(TokenType::OrOr)) {
      int evaluateRight = emit(Op::JumpIfFalse, 0);
      int end = emit(Op::Jump, 0);
      patchJump(evaluateRight);
      emit(Op::Pop);
      logicalAnd();
      patchJump(end);
    }
  }
  void logicalAnd() {
    equality();
    while (match(TokenType::AndAnd)) {
      int end = emit(Op::JumpIfFalse, 0);
      emit(Op::Pop);
      equality();
      patchJump(end);
    }
  }
  void equality() {
    comparison();
    while (match(TokenType::EqualEqual) || match(TokenType::BangEqual)) {
      TokenType op = previous().type;
      comparison();
      emit(op == TokenType::EqualEqual ? Op::Equal : Op::NotEqual);
    }
  }
  void comparison() {
    term();
    while (match(TokenType::Less) || match(TokenType::LessEqual) ||
           match(TokenType::Greater) || match(TokenType::GreaterEqual)) {
      TokenType op = previous().type;
      term();
      if (op == TokenType::Less) emit(Op::Less);
      else if (op == TokenType::LessEqual) emit(Op::LessEqual);
      else if (op == TokenType::Greater) emit(Op::Greater);
      else emit(Op::GreaterEqual);
    }
  }
  void term() {
    factor();
    while (match(TokenType::Plus) || match(TokenType::Minus)) {
      TokenType op = previous().type;
      factor();
      emit(op == TokenType::Plus ? Op::Add : Op::Subtract);
    }
  }
  void factor() {
    unary();
    while (match(TokenType::Star) || match(TokenType::Slash) || match(TokenType::Percent)) {
      TokenType op = previous().type;
      unary();
      if (op == TokenType::Star) emit(Op::Multiply);
      else if (op == TokenType::Slash) emit(Op::Divide);
      else emit(Op::Modulo);
    }
  }
  void unary() {
    if (match(TokenType::Bang)) { unary(); emit(Op::Not); }
    else if (match(TokenType::Minus)) { unary(); emit(Op::Negate); }
    else call();
  }
  void call() {
    primary();
    while (match(TokenType::LeftParen)) {
      int argumentCount = 0;
      if (!check(TokenType::RightParen)) {
        do {
          if (argumentCount == 255) throw Error(peek().line, "Too many arguments");
          expression();
          ++argumentCount;
        } while (match(TokenType::Comma));
      }
      consume(TokenType::RightParen, "Expected ')' after arguments");
      emit(Op::Call, argumentCount);
    }
  }
  void primary() {
    if (match(TokenType::Number)) emit(Op::Constant, addConstant(std::stod(previous().text)));
    else if (match(TokenType::String)) emit(Op::Constant, addConstant(previous().text));
    else if (match(TokenType::True)) emit(Op::TrueValue);
    else if (match(TokenType::False)) emit(Op::FalseValue);
    else if (match(TokenType::Null)) emit(Op::NullValue);
    else if (match(TokenType::Undefined)) emit(Op::Undefined);
    else if (match(TokenType::Identifier)) {
      Token name = previous();
      if (name.text == "print" && check(TokenType::LeftParen)) {
        ++current_;
        int count = 0;
        if (!check(TokenType::RightParen)) {
          do { expression(); ++count; } while (match(TokenType::Comma));
        }
        consume(TokenType::RightParen, "Expected ')' after print arguments");
        emit(Op::Print, count, name.line);
      } else {
        emit(Op::GetName, addName(name.text), name.line);
      }
    } else if (match(TokenType::LeftParen)) {
      expression();
      consume(TokenType::RightParen, "Expected ')' after expression");
    } else {
      throw Error(peek().line, "Expected expression");
    }
  }
};

static bool valuesEqual(const Value& a, const Value& b) {
  if (a.index() != b.index()) return false;
  if (std::holds_alternative<std::monostate>(a) || std::holds_alternative<Null>(a)) return true;
  if (auto value = std::get_if<bool>(&a)) return *value == std::get<bool>(b);
  if (auto value = std::get_if<double>(&a)) return *value == std::get<double>(b);
  if (auto value = std::get_if<std::string>(&a)) return *value == std::get<std::string>(b);
  return std::get<FunctionRef>(a).index == std::get<FunctionRef>(b).index;
}

class VM {
 public:
  explicit VM(Program program) : program_(std::move(program)) {}

  Value run() {
    frames_.push_back({&program_.main, 0, {}, 0, true});
    while (!frames_.empty()) {
      Frame& frame = frames_.back();
      if (frame.ip >= frame.chunk->code.size()) runtimeError(0, "Instruction pointer escaped bytecode");
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
  };
  Program program_;
  std::vector<Value> stack_;
  std::vector<Frame> frames_;
  std::unordered_map<std::string, Binding> globals_;
  Value lastResult_;

  [[noreturn]] void runtimeError(int line, const std::string& message) const { throw Error(line, message); }
  Value pop(int line) {
    if (stack_.empty()) runtimeError(line, "Stack underflow");
    Value value = std::move(stack_.back());
    stack_.pop_back();
    return value;
  }
  const Value& top(int line) const {
    if (stack_.empty()) runtimeError(line, "Stack underflow");
    return stack_.back();
  }
  double number(Value value, int line, const char* operation) const {
    if (auto result = std::get_if<double>(&value)) return *result;
    runtimeError(line, std::string(operation) + " requires numbers");
  }
  const std::string& name(const Frame& frame, int index, int line) const {
    if (index < 0 || static_cast<std::size_t>(index) >= frame.chunk->names.size()) runtimeError(line, "Bad name index");
    return frame.chunk->names[static_cast<std::size_t>(index)];
  }
  void binaryNumber(const Instruction& instruction, Op op) {
    double right = number(pop(instruction.line), instruction.line, "Operator");
    double left = number(pop(instruction.line), instruction.line, "Operator");
    switch (op) {
      case Op::Subtract: stack_.push_back(left - right); break;
      case Op::Multiply: stack_.push_back(left * right); break;
      case Op::Divide:
        if (right == 0.0) runtimeError(instruction.line, "Division by zero");
        stack_.push_back(left / right); break;
      case Op::Modulo:
        if (right == 0.0) runtimeError(instruction.line, "Modulo by zero");
        stack_.push_back(std::fmod(left, right)); break;
      case Op::Less: stack_.push_back(left < right); break;
      case Op::LessEqual: stack_.push_back(left <= right); break;
      case Op::Greater: stack_.push_back(left > right); break;
      case Op::GreaterEqual: stack_.push_back(left >= right); break;
      default: break;
    }
  }

  void execute(const Instruction& in) {
    Frame& frame = frames_.back();
    switch (in.op) {
      case Op::Constant:
        if (in.operand < 0 || static_cast<std::size_t>(in.operand) >= frame.chunk->constants.size()) runtimeError(in.line, "Bad constant index");
        stack_.push_back(frame.chunk->constants[static_cast<std::size_t>(in.operand)]); break;
      case Op::Undefined: stack_.emplace_back(std::monostate{}); break;
      case Op::NullValue: stack_.emplace_back(Null{}); break;
      case Op::TrueValue: stack_.emplace_back(true); break;
      case Op::FalseValue: stack_.emplace_back(false); break;
      case Op::GetName: {
        const std::string key = name(frame, in.operand, in.line);
        auto local = frame.locals.find(key);
        if (local != frame.locals.end()) stack_.push_back(local->second.value);
        else {
          auto global = globals_.find(key);
          if (global == globals_.end()) runtimeError(in.line, "Undefined variable '" + key + "'");
          stack_.push_back(global->second.value);
        }
        break;
      }
      case Op::DefineName:
      case Op::DefineConst: {
        const std::string key = name(frame, in.operand, in.line);
        Value value = pop(in.line);
        const bool mutableBinding = in.op == Op::DefineName;
        if (frame.main) globals_[key] = Binding{std::move(value), mutableBinding};
        else frame.locals[key] = Binding{std::move(value), mutableBinding};
        break;
      }
      case Op::SetName: {
        const std::string key = name(frame, in.operand, in.line);
        auto local = frame.locals.find(key);
        if (local != frame.locals.end()) {
          if (!local->second.mutableBinding) runtimeError(in.line, "Assignment to constant '" + key + "'");
          local->second.value = top(in.line);
        }
        else {
          auto global = globals_.find(key);
          if (global == globals_.end()) runtimeError(in.line, "Assignment to undefined variable '" + key + "'");
          if (!global->second.mutableBinding) runtimeError(in.line, "Assignment to constant '" + key + "'");
          global->second.value = top(in.line);
        }
        break;
      }
      case Op::Pop: pop(in.line); break;
      case Op::Add: {
        Value right = pop(in.line), left = pop(in.line);
        if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right))
          stack_.push_back(valueToString(left) + valueToString(right));
        else stack_.push_back(number(left, in.line, "+") + number(right, in.line, "+"));
        break;
      }
      case Op::Subtract: case Op::Multiply: case Op::Divide: case Op::Modulo:
      case Op::Less: case Op::LessEqual: case Op::Greater: case Op::GreaterEqual:
        binaryNumber(in, in.op); break;
      case Op::Negate: stack_.push_back(-number(pop(in.line), in.line, "Unary -")); break;
      case Op::Not: stack_.push_back(!truthy(pop(in.line))); break;
      case Op::Equal: { Value b = pop(in.line), a = pop(in.line); stack_.push_back(valuesEqual(a, b)); break; }
      case Op::NotEqual: { Value b = pop(in.line), a = pop(in.line); stack_.push_back(!valuesEqual(a, b)); break; }
      case Op::And: { Value b = pop(in.line), a = pop(in.line); stack_.push_back(truthy(a) && truthy(b)); break; }
      case Op::Or: { Value b = pop(in.line), a = pop(in.line); stack_.push_back(truthy(a) || truthy(b)); break; }
      case Op::Jump: frame.ip = static_cast<std::size_t>(in.operand); break;
      case Op::JumpIfFalse: if (!truthy(top(in.line))) frame.ip = static_cast<std::size_t>(in.operand); break;
      case Op::Call: callFunction(in); break;
      case Op::Print: {
        if (in.operand < 0 || stack_.size() < static_cast<std::size_t>(in.operand)) runtimeError(in.line, "Bad print call");
        const std::size_t start = stack_.size() - static_cast<std::size_t>(in.operand);
        for (std::size_t i = start; i < stack_.size(); ++i) {
          if (i != start) std::cout << ' ';
          std::cout << valueToString(stack_[i]);
        }
        std::cout << '\n';
        stack_.resize(start);
        stack_.emplace_back(std::monostate{});
        break;
      }
      case Op::Return: returnFromFunction(in.line); break;
    }
  }

  void callFunction(const Instruction& in) {
    if (in.operand < 0 || stack_.size() < static_cast<std::size_t>(in.operand + 1)) runtimeError(in.line, "Bad function call");
    const std::size_t calleeIndex = stack_.size() - static_cast<std::size_t>(in.operand) - 1;
    auto reference = std::get_if<FunctionRef>(&stack_[calleeIndex]);
    if (!reference || reference->index >= program_.functions.size()) runtimeError(in.line, "Value is not callable");
    const Function& function = program_.functions[reference->index];
    if (function.parameters.size() != static_cast<std::size_t>(in.operand)) {
      runtimeError(in.line, "Function '" + function.name + "' expected " +
                   std::to_string(function.parameters.size()) + " arguments but received " + std::to_string(in.operand));
    }
    Frame next{&function.chunk, 0, {}, calleeIndex, false};
    for (int i = 0; i < in.operand; ++i)
      next.locals[function.parameters[static_cast<std::size_t>(i)]] =
          Binding{stack_[calleeIndex + 1 + static_cast<std::size_t>(i)], true};
    stack_.resize(calleeIndex);
    frames_.push_back(std::move(next));
  }

  void returnFromFunction(int line) {
    Value result = pop(line);
    const std::size_t base = frames_.back().stackBase;
    const bool main = frames_.back().main;
    frames_.pop_back();
    stack_.resize(base);
    if (main) lastResult_ = std::move(result);
    else stack_.push_back(std::move(result));
  }
};

static const char* tokenName(TokenType type) {
  switch (type) {
    case TokenType::End: return "END"; case TokenType::Identifier: return "IDENTIFIER";
    case TokenType::Number: return "NUMBER"; case TokenType::String: return "STRING";
    default: return "TOKEN";
  }
}

static const char* opName(Op op) {
  switch (op) {
    case Op::Constant: return "CONSTANT"; case Op::Undefined: return "UNDEFINED";
    case Op::NullValue: return "NULL"; case Op::TrueValue: return "TRUE";
    case Op::FalseValue: return "FALSE"; case Op::GetName: return "GET_NAME";
    case Op::DefineName: return "DEFINE_NAME"; case Op::DefineConst: return "DEFINE_CONST";
    case Op::SetName: return "SET_NAME";
    case Op::Pop: return "POP"; case Op::Add: return "ADD"; case Op::Subtract: return "SUBTRACT";
    case Op::Multiply: return "MULTIPLY"; case Op::Divide: return "DIVIDE";
    case Op::Modulo: return "MODULO"; case Op::Negate: return "NEGATE"; case Op::Not: return "NOT";
    case Op::Equal: return "EQUAL"; case Op::NotEqual: return "NOT_EQUAL"; case Op::Less: return "LESS";
    case Op::LessEqual: return "LESS_EQUAL"; case Op::Greater: return "GREATER";
    case Op::GreaterEqual: return "GREATER_EQUAL"; case Op::And: return "AND"; case Op::Or: return "OR";
    case Op::Jump: return "JUMP"; case Op::JumpIfFalse: return "JUMP_IF_FALSE";
    case Op::Call: return "CALL"; case Op::Print: return "PRINT"; case Op::Return: return "RETURN";
  }
  return "?";
}

static void dumpChunk(const Chunk& chunk, const std::string& label) {
  std::cout << "== " << label << " ==\n";
  for (std::size_t i = 0; i < chunk.code.size(); ++i) {
    const Instruction& in = chunk.code[i];
    std::cout << std::setw(4) << i << "  " << std::setw(4) << in.line << "  "
              << std::left << std::setw(16) << opName(in.op) << std::right;
    if (in.operand != 0 || in.op == Op::Constant || in.op == Op::GetName ||
        in.op == Op::DefineName || in.op == Op::DefineConst || in.op == Op::SetName) std::cout << in.operand;
    std::cout << '\n';
  }
}

static std::string readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Could not open '" + path + "'");
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

static void executeSource(const std::string& source, bool showTokens, bool showBytecode) {
  std::vector<Token> tokens = Lexer(source).scan();
  if (showTokens) {
    for (const Token& token : tokens)
      std::cout << token.line << "\t" << tokenName(token.type) << "\t" << token.text << '\n';
  }
  Program program = Compiler(std::move(tokens)).compile();
  if (showBytecode) {
    dumpChunk(program.main, "main");
    for (const Function& function : program.functions) dumpChunk(function.chunk, function.name);
  }
  VM(std::move(program)).run();
}

}  // namespace spark

int main(int argc, char** argv) {
  bool showTokens = false;
  bool showBytecode = false;
  std::optional<std::string> filename;
  for (int i = 1; i < argc; ++i) {
    std::string argument = argv[i];
    if (argument == "--tokens") showTokens = true;
    else if (argument == "--bytecode") showBytecode = true;
    else if (argument == "--help") {
      std::cout << "SparkJS 0.1\nUsage: sparkjs [--tokens] [--bytecode] [file.js]\n";
      return 0;
    } else if (!filename) filename = argument;
    else { std::cerr << "Unexpected argument: " << argument << '\n'; return 64; }
  }

  try {
    if (filename) {
      spark::executeSource(spark::readFile(*filename), showTokens, showBytecode);
    } else {
      std::cout << "SparkJS 0.1 REPL (Ctrl-D to exit)\n";
      std::string line;
      while (std::cout << "> " && std::getline(std::cin, line)) {
        try { spark::executeSource(line, showTokens, showBytecode); }
        catch (const spark::Error& error) { std::cerr << "[line " << error.line << "] " << error.what() << '\n'; }
      }
      std::cout << '\n';
    }
  } catch (const spark::Error& error) {
    std::cerr << "[line " << error.line << "] " << error.what() << '\n';
    return 65;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 66;
  }
  return 0;
}
