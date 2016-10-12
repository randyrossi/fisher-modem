class StdioInitException {
 public:
  StdioInitException() {}
  ~StdioInitException() {}
  void getMessage() { fprintf(stderr, "stdio initialization exception\n"); }
};

class StdoutBufferOverflowException {
 public:
  StdoutBufferOverflowException() {}
  ~StdoutBufferOverflowException() {}
  void getMessage() { fprintf(stderr, "stdout buffer overflow exception\n"); }
};

class StdinReadException {
 public:
  StdinReadException() {}
  ~StdinReadException() {}
  void getMessage() { fprintf(stderr, "stdin read exception\n"); }
};

class StdoutWriteException {
 public:
  StdoutWriteException() {}
  ~StdoutWriteException() {}
  void getMessage() { fprintf(stderr, "stdout write exception\n"); }
};
