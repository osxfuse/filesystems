#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>

#include <iostream>
#include <string>
#include <vector>

using std::cout;
using std::flush;
using std::endl;
using std::string;
using std::vector;

// Note: typeof() is a gcc-ism
#define ASSERT_OP(a, op, b) \
  do { \
    typeof(a) _a = (a); \
    typeof(b) _b = (b); \
    if (!(_a op _b)) { \
      std::cout << __FILE__ << ":" << __LINE__ \
                << ", Assertion failed: " \
                << "expected: (" #a ")" #op "(" #b "), " \
                << "actual: (" << _a << ")" #op "(" << _b << ")" \
                << std::endl; \
      exit(1); \
    } \
  } while (0);

#define ASSERT_EQ(a, b) ASSERT_OP(a, ==, b)
#define ASSERT_NE(a, b) ASSERT_OP(a, !=, b)
#define ASSERT_GT(a, b) ASSERT_OP(a, >, b)
#define ASSERT_GE(a, b) ASSERT_OP(a, >=, b)
#define ASSERT_LT(a, b) ASSERT_OP(a, <, b)
#define ASSERT_LE(a, b) ASSERT_OP(a, <=, b)

void fcheckMode(int fd, bool is_dir, mode_t mode) {
  struct stat sb;
  int result = fstat(fd, &sb);
  ASSERT_EQ(result, 0);// fstat on fd failed
  ASSERT_EQ((sb.st_mode & 0777), mode);// mode on file failed
  ASSERT_EQ(((sb.st_mode & S_IFDIR) != 0), (is_dir != 0));// wrong kind of file/directory
}

void writeAll(int fd, const char *buf, size_t nbytes) {
  ssize_t total = 0;
  ssize_t result = 0;
  while (total < nbytes) {
    result = write(fd, buf + total, nbytes - total);
    ASSERT_GT(result, 0);// writing failed to fd
    total += result;
  }
}

void readAll(int fd, char *buf, size_t nbytes) {
  ssize_t total = 0;
  ssize_t result = 0;
  while (total < nbytes) {
    result = read(fd, buf + total, nbytes - total);
    ASSERT_GT(result, 0);// reading failed to fd
    total += result;
  }
}

// rather than fill buffer w/ random data, use something deterministic
// to avoid flakey tests (i.e., tests that sometimes fail).
void fillBufferWithDeterministicBytes(char *buf, size_t size) {
  // this algo from Wikipedia (url broken over two lines)
  // http://en.wikipedia.org/w/index.php?title=
  // Linear_feedback_shift_register&oldid=197926966
  // According to Wikipedia, this will repeat every 2^32 - 1 times.
  // However it may repeat more often here since we only take the lower
  // 8 bits of every state.

  static uint32_t lfsr = 1;
  for (int i = 0; i < size; i++) {
    lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xd0000001u);
    buf[i] = lfsr;  // truncated to 8 bits
  }
}

void testOpenReadWriteClose(const string &path) {
  string filename(path + "/foo.bar");
  mode_t mode = 0644;
  
  int buf_size = 8000;
  vector<char> buf(buf_size);
  fillBufferWithDeterministicBytes(&buf[0], buf.size());
  vector<char> buf2(buf_size);
  
  int fd = open(filename.c_str(), O_CREAT | O_RDWR | O_EXCL, mode);
  if (fd < 0)
    cout << "open of '" << filename << "' failed." << endl;
  ASSERT_GE(fd, 0);// create file read-write failed
  fcheckMode(fd, false, mode);

  writeAll(fd, &buf[0], buf_size);
  lseek(fd, 0, SEEK_SET);
  readAll(fd, &buf2[0], buf_size);
  for (int i = 0; i < buf_size; i++)
    ASSERT_EQ(buf[i], buf2[i]);// read/write differ
  
  int result = close(fd);
  ASSERT_EQ(result, 0);// close failed
  result = unlink(filename.c_str());
  ASSERT_EQ(result, 0);// unlink failed
  
  // open w/ bits for read-only
  mode = 0400;
  
  fd = open(filename.c_str(), O_CREAT | O_EXCL | O_WRONLY, mode);
  ASSERT_GE(fd, 0);// open 0444 for writing failed
  writeAll(fd, &buf[0], buf.size());
  fcheckMode(fd, false, mode);
  result = close(fd);
  ASSERT_EQ(result, 0);// close failed
  result = unlink(filename.c_str());
  ASSERT_EQ(result, 0);// unlink failed
}

void testMoveOverOpenFile(const string &path) {
  string a_file(path + "/foo");
  string b_file(path + "/bar");

  int buf_size = 100;
  vector<char> a_buf(buf_size);
  fillBufferWithDeterministicBytes(&a_buf[0], a_buf.size());
  vector<char> b_buf(buf_size);
  fillBufferWithDeterministicBytes(&b_buf[0], b_buf.size());

  // make file, otherfile
  int a_fd = open(a_file.c_str(), O_CREAT | O_EXCL | O_RDWR, 0400);
  ASSERT_GE(a_fd, 0);// open failed
  writeAll(a_fd, &a_buf[0], a_buf.size());

  int b_fd = open(b_file.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0400);
  ASSERT_GE(b_fd, 0);// open failed
  writeAll(b_fd, &b_buf[0], b_buf.size());
  
  // move b over a
  int result = rename(b_file.c_str(), a_file.c_str());
  ASSERT_EQ(result, 0);// rename failed
  
  int result_fd = open(a_file.c_str(), O_RDONLY, 0);
  ASSERT_GE(result_fd, 0);// open failed
  
  vector<char> result_buf(buf_size);
  
  readAll(result_fd, &result_buf[0], result_buf.size());
  for (int i = 0; i < buf_size; i++)
    ASSERT_EQ(result_buf[i], b_buf[i]);// file changed in move
  
  lseek(a_fd, 0, SEEK_SET);
  
  vector<char> new_a_buf(buf_size);
  
  readAll(a_fd, &new_a_buf[0], new_a_buf.size());
  for (int i = 0; i < buf_size; i++)
    ASSERT_EQ(a_buf[i], new_a_buf[i]);// open file got clobbered by rename

  result = unlink(a_file.c_str());
  ASSERT_EQ(result, 0);// unlink failed

  result = close(a_fd);
  ASSERT_EQ(result, 0);
  result = close(b_fd);
  ASSERT_EQ(result, 0);
  result = close(result_fd);
  ASSERT_EQ(result, 0);
}

void testMoveOverEmptyFolder(const string &path) {
  string a_name(path + "/a");
  string b_name(path + "/b");
  
  int result = mkdir(a_name.c_str(), 0200);
  ASSERT_EQ(result, 0);// mkdir failed
  result = mkdir(b_name.c_str(), 0200);
  ASSERT_EQ(result, 0);// mkdir failed
  
  // rename b over a
  result = rename(b_name.c_str(), a_name.c_str());
  ASSERT_EQ(result, 0);// rename of dirs failed

  result = rmdir(a_name.c_str());
  ASSERT_EQ(result, 0);// rmdir failed
}

void testMoveOverReadOnlyFile(const string &path) {
  string a_file(path + "/foo");
  string b_file(path + "/bar");

  int buf_size = 100;
  vector<char> a_buf(buf_size);
  fillBufferWithDeterministicBytes(&a_buf[0], a_buf.size());
  vector<char> b_buf(buf_size);
  fillBufferWithDeterministicBytes(&b_buf[0], b_buf.size());

  // make file, otherfile
  int a_fd = open(a_file.c_str(), O_CREAT | O_EXCL | O_RDWR, 0400);
  ASSERT_GE(a_fd, 0);// open failed
  writeAll(a_fd, &a_buf[0], a_buf.size());
  int result = close(a_fd);
  ASSERT_EQ(result, 0);

  int b_fd = open(b_file.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0400);
  ASSERT_GE(b_fd, 0);// open failed
  writeAll(b_fd, &b_buf[0], b_buf.size());
  result = close(b_fd);
  ASSERT_EQ(result, 0);
  
  // move b over a
  result = rename(b_file.c_str(), a_file.c_str());
  ASSERT_EQ(result, 0);// rename failed
  
  // open, read, close "new" file a
  int result_fd = open(a_file.c_str(), O_RDONLY, 0);
  ASSERT_GE(result_fd, 0);// open failed
  
  vector<char> result_buf(buf_size);
  readAll(result_fd, &result_buf[0], result_buf.size());
  result = close(result_fd);
  ASSERT_EQ(result, 0);

  // make sure new data match old b data
  for (int i = 0; i < buf_size; i++)
    ASSERT_EQ(result_buf[i], b_buf[i]);// file changed in move
}

void readlinkOnFile(const string &path) {
  const string file(path + "/regular_file.txt");

  int buf_size = 100;
  vector<char> buf(buf_size);
  fillBufferWithDeterministicBytes(&buf[0], buf.size());

  // make file, otherfile
  int fd = open(file.c_str(), O_CREAT | O_EXCL | O_RDWR, 0400);
  ASSERT_GE(fd, 0);// open failed
  writeAll(fd, &buf[0], buf.size());
  int result = close(fd);
  ASSERT_EQ(result, 0);

  // now, try to readlink the file. should fail
  vector<char> rl_buf(buf_size);
  ssize_t rl_result = readlink(file.c_str(), &rl_buf[0], rl_buf.size());
  ASSERT_EQ(rl_result, -1);
  ASSERT_EQ(errno, EINVAL);
}

void testSetGetAttributes(const string &path) {
  // check mode on file, dir, and size on file
  // TODO(adlr): check mod/access times
  string file(path + "/file.txt");
  string dir(path + "/dir");
  
  // create dir
  int result = mkdir(dir.c_str(), 0600);
  if (result != 0)
    perror("mkdir");
  ASSERT_EQ(result, 0);

  // create single byte file
  int fd = open(file.c_str(), O_CREAT | O_EXCL | O_RDWR, 0000);
  ASSERT_GE(fd, 0);// open failed
  vector<char> databuf(1);
  databuf[0] = 'x';
  writeAll(fd, &databuf[0], databuf.size());
  result = close(fd);
  ASSERT_EQ(result, 0);
  
  for (int i = 0; i < 0777; i++) {
    // set mode
    result = chmod(dir.c_str(), i);
    ASSERT_EQ(result, 0);
    result = chmod(file.c_str(), i);
    ASSERT_EQ(result, 0);
    
    // check mode
    struct stat stbuf;
    result = stat(dir.c_str(), &stbuf);
    ASSERT_EQ(result, 0);
    ASSERT_EQ((stbuf.st_mode & S_IFMT), S_IFDIR);
    ASSERT_EQ((stbuf.st_mode & 0777), i);

    result = stat(file.c_str(), &stbuf);
    ASSERT_EQ(result, 0);
    ASSERT_EQ((stbuf.st_mode & S_IFMT), S_IFREG);
    ASSERT_EQ((stbuf.st_mode & 0777), i);
    ASSERT_EQ(stbuf.st_size, databuf.size());
  }
  
  // make sure symlink and readlink work
  string slink(path + "/symlink");
  char *link_path = "foobar";
  result = symlink(link_path, slink.c_str());
  ASSERT_EQ(result, 0);
  vector<char> readlinkbuf(strlen(link_path) + 2);
  result = readlink(slink.c_str(), &readlinkbuf[0], readlinkbuf.size());
  ASSERT_EQ(result, strlen(link_path));
  for (int i = 0; i < strlen(link_path); i++)
    ASSERT_EQ(link_path[i], readlinkbuf[i]);
  
  // make sure lstat works
  struct stat linkstatbuf;
  result = lstat(slink.c_str(), &linkstatbuf);
  ASSERT_EQ(result, 0);
  ASSERT_EQ((linkstatbuf.st_mode & S_IFMT), S_IFLNK);
}

void testRemoveNonEmptyFolder(const string &path) {
  string dir(path + "/dir");
  string innerdir(path + "/dir/inner");
  
  // create dir
  int result = mkdir(dir.c_str(), 0700);
  ASSERT_EQ(result, 0);
  result = mkdir(innerdir.c_str(), 0600);
  ASSERT_EQ(result, 0);

  result = rmdir(dir.c_str());
  ASSERT_EQ(result, -1);
  ASSERT_EQ(errno, ENOTEMPTY);
}

void usage(const string &me) {
  cout << "usage: " << me << " /path/to/empty/filesystem/dir" << endl;
  exit(1);
}

void setUp(const string &path) {
  // TODO(adlr): make sure path is empty w/o a crappy shell script
  string command(string("ls -A \"") + path +
                 "\" | awk '{bad = 1;} "
                 "END {if (bad) {print \"supplied dir not empty!\";}}'");
  system(command.c_str());
}

void tearDown(const string &path) {
  // TODO(adlr): don't use shell commands
  string command(string("chmod -R a+rwx \"") + path + "\"");
  //cout << "tearDown: " << command << endl;
  system(command.c_str());
  command = string("rm -rf \"") + path + "\"/*";
  //cout << "tearDown: " << command << endl;
  system(command.c_str());
}

#define RUN_TEST(x, y) \
  do { \
    setUp(y); \
    cout << "running " #x "..." << flush; \
    x(y); \
    cout << " success" << endl; \
    tearDown(y); \
  } while (0);

int main (int argc, char const *argv[]) {
  if (argc != 2)
    usage(argv[0]);
  RUN_TEST(testOpenReadWriteClose, argv[1]);
  RUN_TEST(testMoveOverOpenFile, argv[1]);
  RUN_TEST(testMoveOverEmptyFolder, argv[1]);
  RUN_TEST(testMoveOverReadOnlyFile, argv[1]);
  RUN_TEST(readlinkOnFile, argv[1]);
  RUN_TEST(testSetGetAttributes, argv[1]);
  RUN_TEST(testRemoveNonEmptyFolder, argv[1]);
  return 0;
}
