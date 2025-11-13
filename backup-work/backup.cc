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

bool check_file(const std::string& file) {
  struct stat stat1;
  if (stat(file.c_str(), &stat1) != 0) {
    const char* error = "ERROR: No se pudieron cargar los datos de ARCHIVO.\n";
    write(STDERR_FILENO, error, strlen(error));
    return false;
  }
  if (access(file.c_str(), F_OK) != 0) {
    const char* error = "ERROR: El archivo no existe\n";
    write(STDERR_FILENO, error, strlen(error));
    return false;
  }
  if (!S_ISREG(stat1.st_mode)) {
    const char* error = "ERROR: El archivo no es regular\n";
    write(STDERR_FILENO, error, strlen(error));
    return false;
  }
  return true;
}

int open_file(const std::string& archivo) {
  int abierto = open(archivo.c_str(), O_RDONLY);
  if (abierto == -1) {
    const char* error = "ERROR: No se pudo abrir el archivo\n";
    write(STDERR_FILENO, error, strlen(error));
    return abierto;
  }
  return abierto;
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

std::string get_absolute_path(const std::string& path) {
  char absolute_path[buffer_size];
  if (realpath(path.c_str(), absolute_path) == NULL) {
    return std::string();
  }
  return std::string(absolute_path);
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
  std::string var_env = get_environment_variable("BACKUP_WORK_DIR");
  if (var_env.empty()) {
    const char* no_existe = "ERROR: La variable de entorno BACKUP_WORK_DIR no está definida.\n";
    write(STDERR_FILENO, no_existe, strlen(no_existe));
    return EXIT_FAILURE;
  }
  std::string file{};
  if (argc != 2) {
    const char* error = "ERROR: No se ha pasado ningún archivo para ser copiado\n";
    write(STDERR_FILENO, error, strlen(error));
    return EXIT_FAILURE;
  } else {
      file = argv[1];
  }
  if (!check_file(file)) {
    return EXIT_FAILURE;
  }
  std::string archivo_pid = var_env + "/backup-server.pid";
  pid_t pid;
  int abierto = open_file(archivo_pid);
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
    pid = std::atoi(pid_obtenido.c_str());
  }
  if (!existe_proceso(pid)) {
    const char* error = "ERROR: El proceso ya existe\n";
    write(STDERR_FILENO, error, strlen(error));
    return EXIT_FAILURE;
  }
  std::string fifo = var_env + "/backup.fifo";
  int abrir_fifo = open(fifo.c_str(), O_WRONLY);
  if (abrir_fifo == -1) {
    const char* error = "ERROR: No se pudo abrir el archivo FIFO\n";
    write(STDERR_FILENO, error, strlen(error));
    return EXIT_FAILURE;
  }
  std::string absolute_path = get_absolute_path(file) + "\n";
  if (absolute_path.empty()) {
    const char* error = "ERROR: No se pudo convertir a dirección absoluta\n";
    write(STDERR_FILENO, error, strlen(error));
    return EXIT_FAILURE;
  }
  int escribir_fifo = write(abrir_fifo, absolute_path.c_str(), absolute_path.length());
  if (escribir_fifo == -1) {
    const char* error = "ERROR: No se pudo escribir en el archivo FIFO\n";
    write(STDERR_FILENO, error, strlen(error));
    return EXIT_FAILURE;
  }
  if (mandar_señal(pid) == -1) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}