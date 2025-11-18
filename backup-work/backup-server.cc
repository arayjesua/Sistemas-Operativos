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
#include <cstdlib>
#include <expected>
#include <atomic>

const int buffer_size = 65536;
std::atomic<bool> quit_requested{false};
sigset_t conjunto_señales;

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

std::expected<pid_t, std::system_error> read_server_pid(const std::string& pid_file_path) {
  int pid_open = open(pid_file_path.c_str(), O_RDONLY);
  if (pid_open == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al abrir el archivo PID\n"));
  }
  std::string pid_file(buffer_size, '\0');
  int pid_read = read(pid_open, pid_file.data(), buffer_size);
  if (pid_read == -1) {
    close(pid_open);
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al leer el PID\n"));
  } else if (pid_read == 0) {
      close(pid_open);
      return std::unexpected(std::system_error(errno, std::system_category(), "PID no válido\n"));
  }
  pid_file.resize(pid_read);
  char* fallo = NULL;
  errno = 0;
  pid_t pid = std::strtol(pid_file.c_str(), &fallo, 10);
  if (fallo == pid_file.c_str()) {
    return std::unexpected(std::system_error(errno, std::system_category(), "PID no válido\n"));
  } else if (errno == ERANGE) {
      return std::unexpected(std::system_error(errno, std::system_category(), "PID no válido\n"));
  } else if (*fallo != '\n') {
      return std::unexpected(std::system_error(errno, std::system_category(), "PID no válido\n"));
  } else if (pid <= 0) {
      return std::unexpected(std::system_error(errno, std::system_category(), "PID no válido\n"));
  }
  close(pid_open);
  return pid;
}

bool is_server_running(pid_t pid) {
  int running = kill(pid, 0);
  if (running == -1) {
    if (errno == ESRCH) {
      return false;
    }
  }
  return true;
}

std::expected<void, std::system_error> create_fifo(const std::string& fifo_path) {
  int tuberia = mkfifo(fifo_path.c_str(), 0666);
  if (tuberia == -1) {
    if (errno == EEXIST) {
      const char* error = "Ya existe un FIFO con ese nombre\nEliminando y creando de nuevo\n";
      write(STDERR_FILENO, error, strlen(error));
      int tuberia_recreada = unlink(fifo_path.c_str());
      if (tuberia_recreada == -1) {
        return std::unexpected(std::system_error(errno, std::system_category(), "No se pudo crear la FIFO"));
      } else {
          return create_fifo(fifo_path);
      }
    } else if (errno == EACCES) {
        return std::unexpected(std::system_error(errno, std::system_category(), "No hay permisos para crear el archivo en el directorio indicado\n"));
    } else if (errno == ENOENT) {
        return std::unexpected(std::system_error(errno, std::system_category(), "Algún componente de la ruta no existe\n"));
    } else {
        const char* error = "Error en la creación del FIFO\n";
        write(STDERR_FILENO, error, strlen(error));
        return std::unexpected(std::system_error(errno, std::system_category(), "Error en la creación del FIFO"));
    }
  }
  return {};
}

std::expected<void, std::system_error> write_pid_file(const std::string& pid_file_path) {
  int pid_file_open = open(pid_file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (pid_file_open == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al abrir el archivo PID\n"));
  }
  pid_t pid = getpid();
  std::string pid_a_enviar = std::to_string(pid) + "\n";
  int written = write(pid_file_open, pid_a_enviar.c_str(), pid_a_enviar.length());
  if (written == -1) {
    close(pid_file_open);
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al escribir en el achivo PID\n"));
  }
  close(pid_file_open);
  return {};
}

void signal_handler(int signum) {
  char message[] = "Señal de terminación recibida, cerrando...\n";
  write(STDOUT_FILENO, message, sizeof(message) - 1);
  quit_requested = true;
}

std::expected<void, std::system_error> setup_signal_handler() {
  struct sigaction sa = {};
  sa.sa_handler = signal_handler;
  int señal = sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (señal == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "No es una señal válida\n"));
  }
  señal = sigaddset(&conjunto_señales, SIGUSR1);
  if (señal == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al añadir SIGUSR1 al set\n"));
  }
  señal = sigprocmask(SIG_BLOCK, &conjunto_señales, NULL);
  if (señal == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al bloquear las señales\n"));
  }
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al configurar el manejador\n"));
  }
  if (sigaction(SIGHUP, &sa, NULL) == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al configurar el manejador\n"));
  }
  if (sigaction(SIGTERM, &sa, NULL) == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al configurar el manejador\n"));
  }
  if (sigaction(SIGQUIT, &sa, NULL) == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al configurar el manejador\n"));
  }
  return {};
}

std::expected<std::string, std::system_error> read_path_from_fifo(int fifo_fd) {
  char ultimo_caracter_leido;
  int tamaño{0};
  std::string path{""};
  for (int i{0}; i < PATH_MAX; ++i) {
    int leido = read(fifo_fd, &ultimo_caracter_leido, 1);
    if (leido == -1) {
      if (errno == EINTR) {
        return std::unexpected(std::system_error(ECANCELED, std::system_category(), "Llamada de finalización"));
      }
      return std::unexpected(std::system_error(errno, std::system_category(), "Error al leer la ruta de la FIFO\n"));
    } else if (leido == 0) {
        break;
    }
    if (ultimo_caracter_leido == '\n') {
      return path;
    } else {
        path += ultimo_caracter_leido;
    }
  }
  return std::unexpected(std::system_error(errno, std::system_category(), "Tamaño máximo excedido"));
}

bool directorio_correcto(const char* direccion) {
  struct stat stat1;
  if (stat(direccion, &stat1) != 0) {
    const char* error = "Error al cargar los datos de DIRECTORIO_DESTINO\n";
    write(STDERR_FILENO, error, strlen(error));
    return false;
  }
  if (!S_ISDIR(stat1.st_mode)) {
    const char* error = "El parámetro no es un directorio\n";
    write(STDERR_FILENO, error, strlen(error));
    return false;
  }
  if (access(direccion, W_OK) != 0) {
    const char* error = "El directorio no es accesible\n";
    write(STDERR_FILENO, error, strlen(error));
    return false;
  }
  return true;
}

std::expected<void, std::system_error> copy_file(const std::string& src_path, const std::string& dest_path, mode_t dst_perms=0) {
  int origen = open(src_path.c_str(), O_RDONLY);
  if (origen == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al abrir el archivo de origen"));
  }
  int destino = open(dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, dst_perms);
  if (destino == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al abrir el archivo de destino"));
  }
  int tamaño;
  std::vector<char> buffer(buffer_size);
  while ((tamaño = read(origen, buffer.data(), buffer_size)) > 0 && errno != EINTR && !quit_requested) {
    write(destino, buffer.data(), tamaño);
  }
  if (errno == EINTR && quit_requested) {
    return std::unexpected(std::system_error(ECANCELED, std::system_category(), "Copia cancelada por terminación"));
  }
  close(origen);
  close(destino);
  return {};
}

void run_server(int fifo_fd, const std::string& backup_dir) {
  siginfo_t sig;
  while (!quit_requested) {
    int espera = sigwaitinfo(&conjunto_señales, &sig);
    if (espera == -1) {
      break;
    }
    if (sig.si_signo == SIGUSR1) {
      auto ruta_origen = read_path_from_fifo(fifo_fd);
      std::string path;
      if (ruta_origen) {
        path = *ruta_origen;
      } else {
          std::system_error error = ruta_origen.error();
          if (error.code() == std::errc::operation_canceled) {
            continue;
          }
          std::string mensaje = error.what() + '\n';
          write(STDERR_FILENO, mensaje.c_str(), mensaje.length());
          break;
      }
      if (path.empty()) {
        close(fifo_fd);
        fifo_fd = open(get_fifo_path().c_str(), O_RDONLY);
        if (fifo_fd == -1) {
          if (quit_requested) {
            break;
          }
        }
        continue;
      }
      std::string archivo = get_filename(path);
      std::string destino_backup = backup_dir + "/" + archivo;
      auto copia = copy_file(path, destino_backup, 0666);
      if (copia.has_value()) {
        const char* exito = "ARCHIVO COPIADO CON ÉXITO\n";
        write (STDOUT_FILENO, exito, std::strlen(exito));
      } else {
          std::system_error error = copia.error();
          std::string msg_error{error.what()};
          msg_error += "\n";
          write(STDERR_FILENO, msg_error.c_str(), msg_error.length());
          continue;
      }
    }
  }
  close(fifo_fd);
  std::string pid_path = get_pid_file_path();
  if (unlink(pid_path.c_str()) != -1) {
    const char* exito = "ARCHIVO PID BORRADO CON ÉXITO\n";
    write (STDOUT_FILENO, exito, std::strlen(exito));
  } else {
      const char* exito = "Error al borrar el archivo PID\n";
      write (STDERR_FILENO, exito, std::strlen(exito));
  }
  std::string fifo_path = get_fifo_path();
  if (unlink(fifo_path.c_str()) != -1) {
    const char* exito = "ARCHIVO FIFO BORRADO CON ÉXITO\n";
    write (STDOUT_FILENO, exito, std::strlen(exito));
  } else {
      const char* exito = "Error al borrar el archivo FIFO\n";
      write (STDERR_FILENO, exito, std::strlen(exito));
  }
}

int main(int argc, char* argv[]) {
  std::string var_env = get_work_dir();
  if (var_env.empty()) {
    const char* no_existe = "La variable de entorno BACKUP_WORK_DIR no está definida.\n";
    write(STDERR_FILENO, no_existe, strlen(no_existe));
    return EXIT_FAILURE;
  }
  if (!directorio_correcto(var_env.c_str())) {
    return EXIT_FAILURE;
  }
  const char* directorio{};
  if (argc == 1) {
    directorio = get_current_dir().c_str();
  } else {
      directorio = argv[1];
  }
  if (!directorio_correcto(directorio)) {
    return EXIT_FAILURE;
  }
  std::string ruta_pid = get_pid_file_path();
  std::string pid;
  int archivo_pid = open(ruta_pid.c_str(), O_RDONLY);
  if (archivo_pid != -1) {
    if (errno == EINTR) {
      if (quit_requested) {
        std::string abortar = "Llamada de terminación\n";
        write(STDERR_FILENO, abortar.c_str(), abortar.length());
        return EXIT_FAILURE;
      }
    }
    std::string buffer_pid(buffer_size, '\0');
    int tamaño = read(archivo_pid, buffer_pid.data(), buffer_size);
    buffer_pid.resize(tamaño);
    pid = buffer_pid;
    close(archivo_pid);
  } else {
      std::string error{"Error al abrir el archivo.\n"};
      write(STDERR_FILENO, error.c_str(), error.length());
  }
  if (!pid.empty()) {
    pid_t pid_server = std::stoi(pid);
    if (is_server_running(pid_server)) {
      const char* existe = "El proceso ya existe.\n";
      write(STDERR_FILENO, existe, strlen(existe));
      return EXIT_FAILURE;
    } else {
        const char* no_existe = "El proceso no existe. Creando...\n";
        write(STDOUT_FILENO, no_existe, strlen(no_existe));
    }
  }
  std::string fifo = get_fifo_path();
  auto error = create_fifo(fifo);
  if (!error.has_value()) {
      std::system_error error_message = error.error();
      std::string message = error_message.what();
      write(STDERR_FILENO, message.c_str(), message.length());
      return EXIT_FAILURE;
  }
  auto error_signal = setup_signal_handler();
  if (!error_signal.has_value()) {
    std::system_error error = error_signal.error();
    std::string message = error.what();
    write(STDERR_FILENO, message.c_str(), message.length());
    return EXIT_FAILURE;
  }

  auto error_pid = write_pid_file(ruta_pid);
  if (!error_pid.has_value()) {
    std::system_error error = error_pid.error();
    std::string message = error.what();
    write(STDERR_FILENO, message.c_str(), message.length());
    unlink(ruta_pid.c_str());
    unlink(fifo.c_str());
    return EXIT_FAILURE;
  }
  int fifo_abierto = open(fifo.c_str(), O_RDONLY);
  if (fifo_abierto == -1) {
    if (errno == EINTR) {
      if (quit_requested) {
        std::string abortar = "Llamada de terminación\n";
        write(STDERR_FILENO, abortar.c_str(), abortar.length());
        unlink(ruta_pid.c_str());
        unlink(fifo.c_str());
        return EXIT_FAILURE;
      }
    }
    const char* error = "Error al abrir el FIFO\n";
    write(STDERR_FILENO, error, strlen(error));
    unlink(ruta_pid.c_str());
    unlink(fifo.c_str());
    return EXIT_FAILURE;
  }
  run_server(fifo_abierto, directorio);
  return EXIT_SUCCESS;
}