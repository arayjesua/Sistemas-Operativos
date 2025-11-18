#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>
#include <signal.h>
#include <vector>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <system_error>
#include <string>
#include <expected>
#include <atomic>

const int buffer_size = 65536;

std::string get_environment_variable(const std::string& name) {
  char* value = getenv(name.c_str());
  if (value) {
    return std::string(value);
  } else {
      return std::string();
  }
}

std::string get_work_dir() {
  return std::string(get_environment_variable("BACKUP_WORK_DIR"));
}

std::string get_fifo_path() {
  std::string dir_trabajo = get_work_dir();
  std::string fifo_path = dir_trabajo + "/backup.fifo";
  return fifo_path;
}

std::string get_pid_file_path() {
  std::string dir_trabajo = get_work_dir();
  std::string pid_path = dir_trabajo + "/backup-server.pid";
  return pid_path;
}

std::expected<std::string, std::system_error> get_absolute_path(const std::string& path) {
  char* absolute_path_buffer = realpath(path.c_str(), nullptr);
  if (absolute_path_buffer == nullptr) {
    free(absolute_path_buffer);
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al convertir la ruta en absoluta\n"));
  }
  std::string absolute_path = absolute_path_buffer;
  free(absolute_path_buffer);
  return absolute_path;
}

bool file_exists(const std::string& path) {
  if (access(path.c_str(), F_OK) != 0) {
    return false;
  }
  return true;
}

bool is_regular_file(const std::string& path) {
  struct stat stat1;
  if (stat(path.c_str(), &stat1) != 0) {
    const char* error = "Error al cargar los datos del archivo\n";
    write(STDERR_FILENO, error, strlen(error));
    return false;
  }
  if (!S_ISREG(stat1.st_mode)) {
    return false;
  }
  return true;
}

bool is_directory(const std::string& path) {
  struct stat es_dir;
  if (stat(path.c_str(), &es_dir) == -1) {
    return false;
  } else {
      if (!S_ISDIR(es_dir.st_mode)) {
        return false;
      }
  }
  return true;
}

std::string get_current_dir() {
  char dir[1024];
  char* current_dir = getcwd(dir, 1024);
  if (current_dir == NULL) {
    const char* error = "Error al obtener el directorio actual\n";
    write(STDERR_FILENO, error, strlen(error));
    return std::string();
  }
  return std::string(current_dir);
}

std::string get_filename(const std::string& path) {
  char* path_copy = new char[path.length() + 1];
  path.copy(path_copy, path.size());
  std::string origen{basename(path_copy)};
  delete[] path_copy;
  return origen;
}

bool check_args(int argc, char* argv[]) {
  if (argc != 2) {
    return false;
  }
  if (is_directory(argv[1])) {
    return false;
  }
  return true;
}

std::expected<void, std::system_error> check_work_dir_exists(const std::string& work_dir) {  
  struct stat stat1;
  if (stat(work_dir.c_str(), &stat1) != 0) {
    return std::unexpected(std::system_error(errno, std::system_category(), "ERROR"));
  }
  if (!is_directory(work_dir)) {
    return std::unexpected(std::system_error(errno, std::system_category(), "ERROR"));
  }
  if (access(work_dir.c_str(), W_OK) != 0) {
    return std::unexpected(std::system_error(errno, std::system_category(), "ERROR"));
  }
  if (access(work_dir.c_str(), R_OK) != 0) {
    return std::unexpected(std::system_error(errno, std::system_category(), "ERROR"));
  }
  return {};
}

std::expected<int, std::system_error> open_fifo_write(const std::string& fifo_path) {
  int abrir_fifo = open(fifo_path.c_str(), O_WRONLY);
  if (abrir_fifo == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al abrir la FIFO para escritura\n"));
  }
  return abrir_fifo;
}

std::expected<void, std::system_error> write_path_to_fifo(int fifo_fd, const std::string& file_path) {
  std::string path = file_path + '\n';
  int escrito = write(fifo_fd, path.data(), path.size());
  if (escrito == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al escribir en la FIFO\n"));
  }
  return {}; 
}

bool existe_proceso(const pid_t& pid) {
  int existe = kill(pid, 0);
  if (existe == -1) {
    if (errno == ESRCH) {
      return false;
    }
  }
  return true;
}

int mandar_señal(const pid_t& pid) {
  int señal = kill(pid, SIGUSR1);
  if (señal == -1) {
    const char* error = "ERROR: No se enviar la señal\n";
    write(STDERR_FILENO, error, strlen(error));
    return señal;
  }
  return señal;
}

int main(int argc, char* argv[]) {
  std::string var_env = get_work_dir();
  auto error_work_dir = check_work_dir_exists(var_env);
  if (!error_work_dir.has_value()) {
    std::system_error error = error_work_dir.error();
    std::string message = error.what();
    write(STDERR_FILENO, message.c_str(), message.length());
    return EXIT_FAILURE;
  }
  std::string file{};
  if (check_args(argc, argv)) {
    file = argv[1];
  } else {
      std::string error = "No se ha pasado un archivo como argumento\n";
      write(STDERR_FILENO, error.c_str(), error.length());
      return EXIT_FAILURE;
  }
  if ((!file_exists(file)) && (!is_regular_file(file))) {
    std::string error = "El archivo no es regular\n";
    write(STDERR_FILENO, error.c_str(), error.length());
    return EXIT_FAILURE;
  }
  std::string archivo_pid = get_pid_file_path();
  pid_t pid;
  int abierto = open(archivo_pid.c_str(), O_RDONLY);
  int tamaño;
  if (abierto != -1) {
    std::string pid_obtenido(buffer_size, '\0');
    tamaño = read(abierto, pid_obtenido.data(), buffer_size);
    if (tamaño == -1) {
      const char* error = "ERROR: No se pudo abrir el archivo PID\n";
      write(STDERR_FILENO, error, strlen(error));
      return EXIT_FAILURE;
    }
    pid_obtenido.resize(tamaño);
    pid = std::stoi(pid_obtenido);
  }
  if (!existe_proceso(pid)) {
    const char* error = "ERROR: El proceso ya existe\n";
    write(STDERR_FILENO, error, strlen(error));
    return EXIT_FAILURE;
  }
  std::string fifo = get_fifo_path();
  int abrir_fifo = open(fifo.c_str(), O_WRONLY);
  if (abrir_fifo == -1) {
    const char* error = "ERROR: No se pudo abrir el archivo FIFO\n";
    write(STDERR_FILENO, error, strlen(error));
    return EXIT_FAILURE;
  }
  auto camino = get_absolute_path(file);
  std::string absolute_path;
  if (camino) {
    absolute_path = *camino + '\n';
  } else {
      std::system_error error = camino.error();
      std::string message = error.what();
      write(STDERR_FILENO, message.c_str(), message.length());
      return EXIT_FAILURE;
  }

  auto error_write = write_path_to_fifo(abrir_fifo, absolute_path);
  if (!error_write.has_value()) {
    std::system_error error = error_write.error();
    std::string message = error.what();
    write(STDERR_FILENO, message.c_str(), message.length());
    return EXIT_FAILURE;
  }
  if (mandar_señal(pid) == -1) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}