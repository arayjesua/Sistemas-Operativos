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
#include <sys/types.h>
#include <sys/wait.h>

const int buffer_size = 65536;
std::atomic<bool> quit_requested{false};
sigset_t conjunto_señales;
struct ServerOptions {
  enum class CompressionType {
    NONE,
    GZIP,
    BZIP2,
    XZ
  };
  CompressionType compression = CompressionType::NONE;
  std::string destino_backup;
};

enum class ParseArgsErrors {
  unknown_option,
  multiple_compression_options,
  too_many_arguments
};

enum class CopyFileCompressedError {
  error_creando_pipe,
  error_escribiendo_en_pipe,
  error_leyendo_de_pipe,
  error_creando_hijo,
  error_abriendo_archivo_destino,
  error_ejecutando_exec,
  error_bloqueando_señal,
  error_abriendo_archivo_origen,
  error_leyendo_archivo_origen,
  error_hijo_terminado_por_señal
};

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

std::string errores(CopyFileCompressedError& error) {
  switch (error) {
    case CopyFileCompressedError::error_abriendo_archivo_destino:
      return "backup-server: error: error abriendo el archivo de destino\n";

    case CopyFileCompressedError::error_abriendo_archivo_origen:
      return "backup-server: error: error abriendo el archivo de origen\n";

    case CopyFileCompressedError::error_bloqueando_señal:
      return "backup-server: error: error bloqueando la señal SIGPIPE\n";

    case CopyFileCompressedError::error_creando_hijo:
      return "backup-server: error: error creando el proceso hijo\n";

    case CopyFileCompressedError::error_creando_pipe:
      return "backup-server: error: error creando el pipe\n";

    case CopyFileCompressedError::error_ejecutando_exec:
      return "backup-server: error: error ejecutando execvp\n";

    case CopyFileCompressedError::error_escribiendo_en_pipe:
      return "backup-server: error: error escribiendo en el pipe\n";

    case CopyFileCompressedError::error_hijo_terminado_por_señal:
      return std::string();

    case CopyFileCompressedError::error_leyendo_archivo_origen:
      return "backup-server: error: error leyendo el archivo de origen\n";

    case CopyFileCompressedError::error_leyendo_de_pipe:
      return "backup-server: error: error leyendo del pipe\n";

    default:
      break;
  }
  return std::string();
}

std::string get_compression_extension(ServerOptions::CompressionType compression) {
  switch (compression) {
    case ServerOptions::CompressionType::BZIP2:
      return ".bz2";
    
    case ServerOptions::CompressionType::GZIP:
      return ".gz";

    case ServerOptions::CompressionType::XZ:
      return ".xz";
    
    default:
      break;
  }
  return std::string();
}

std::expected<void, CopyFileCompressedError> copy_file_compressed(const std::string& src_path, const std::string& dest_path, const std::string& compression_command) {
  int pipefd[2];
  int fallo = pipe(pipefd);
  if (fallo == -1) {
    return std::unexpected(CopyFileCompressedError::error_creando_pipe);
  }
  pid_t hijo = fork();
  if (hijo == -1) {
    return std::unexpected(CopyFileCompressedError::error_creando_hijo);
  } else if (hijo == 0) {
      dup2(pipefd[0], 0);
      close(pipefd[0]);
      close(pipefd[1]);
      int abierto = open(dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (abierto == -1) {
        return std::unexpected(CopyFileCompressedError::error_abriendo_archivo_destino);
      }
      dup2(abierto, 1);
      close(abierto);
      char* args[] = { const_cast<char*>(compression_command.c_str()), nullptr };
      execvp(compression_command.c_str(), args);
      return std::unexpected(CopyFileCompressedError::error_ejecutando_exec);
      std::exit(EXIT_SUCCESS);
  } else {
      int señal = sigemptyset(&conjunto_señales);
      if (señal == -1) {
        return std::unexpected(CopyFileCompressedError::error_bloqueando_señal);
      }
      señal = sigaddset(&conjunto_señales, SIGUSR1);
      if (señal == -1) {
        return std::unexpected(CopyFileCompressedError::error_bloqueando_señal);
      }
      señal = sigprocmask(SIG_BLOCK, &conjunto_señales, NULL);
      if (señal == -1) {
        return std::unexpected(CopyFileCompressedError::error_bloqueando_señal);
      }
      close(pipefd[0]);
      int tamaño;
      std::string datos(buffer_size, '\0');
      int abierto = open(src_path.c_str(), O_RDONLY);
      if (abierto == -1) {
        return std::unexpected(CopyFileCompressedError::error_abriendo_archivo_origen);
      }
      while ((tamaño = read(abierto, datos.data(), buffer_size)) > 0) {
        datos.resize(tamaño);
        write(pipefd[1], datos.data(), tamaño);
      }
      if (tamaño == -1) {
        return std::unexpected(CopyFileCompressedError::error_leyendo_archivo_origen);
      }
      close(pipefd[1]);
      int status;
      waitpid(hijo, &status, 0);
      if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        std::string correcto = "Hijo terminó con código ";
        correcto += std::to_string(exit_code) + '\n';
        write(STDOUT_FILENO, correcto.c_str(), correcto.length());
      } else if (WIFSIGNALED(status)) {
          int signal = WTERMSIG(status);
          std::string terminado = "Hijo terminado por señal ";
          terminado += std::to_string(signal);
          write(STDOUT_FILENO, terminado.c_str(), terminado.length());
          return std::unexpected(CopyFileCompressedError::error_hijo_terminado_por_señal);
      }
  }
  return {};
}

std::string get_compression_command(ServerOptions::CompressionType compression) {
  std::string compression_type;
  switch (compression) {
    case ServerOptions::CompressionType::BZIP2:
      compression_type = "bzip2";
      break;
    
    case ServerOptions::CompressionType::GZIP:
      compression_type = "gzip";
      break;

    case ServerOptions::CompressionType::XZ:
      compression_type = "xz";
      break;

    default:
      compression_type = "none";
      break;
  }
  return compression_type;
}

void run_server(int fifo_fd, const std::string& backup_dir, ServerOptions& opciones) {
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
      if (opciones.compression == ServerOptions::CompressionType::NONE) {
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
      } else {
          std::string extension = get_compression_extension(opciones.compression);
          destino_backup += extension;
          std::string comando = get_compression_command(opciones.compression);
          auto copia = copy_file_compressed(path, destino_backup, comando);
          if (copia.has_value()) {
            std::string exito = "backup completado: ";
            exito += path + " -> " + destino_backup + '\n';
            write (STDOUT_FILENO, exito.c_str(), exito.length());
          } else {
            CopyFileCompressedError error = copia.error();
            std::string msg_error{errores(error)};
            write(STDERR_FILENO, msg_error.c_str(), msg_error.length());
            continue;
          }
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

std::expected<ServerOptions, ParseArgsErrors> parse_arguments(int argc, char* argv[]) {
  ServerOptions opciones;
  bool flag{false};
  for (int opt; (opt = getopt(argc, argv, "zjx")) != -1;) {
    if (!flag) {
      switch (opt) {
        case 'z':
          opciones.compression = ServerOptions::CompressionType::GZIP; 
          flag = true;
          break;
        
        case 'j':
          opciones.compression = ServerOptions::CompressionType::BZIP2;
          flag = true;
          break;
        
        case 'x':
          opciones.compression =  ServerOptions::CompressionType::XZ;
          flag = true;
          break;
        
        default:
          return std::unexpected(ParseArgsErrors::unknown_option);
      }
    } else if ((opt == 'z' || opt == 'j' || opt == 'x') && flag) {
        return std::unexpected(ParseArgsErrors::multiple_compression_options);
    }
  }
  if (optind < argc) {
    opciones.destino_backup = argv[optind];
  }
  if (optind == argc) {
    if (argc == 1 || (argc == 2) && flag) {
      opciones.destino_backup = get_current_dir();
    }
  }
  if (argc > 3) {
    return std::unexpected(ParseArgsErrors::too_many_arguments);
  }
  return opciones;
}

void write_usage() {
  std::string usage = "uso: backup-server [-z | -j | -x] [DIRECTORIO_DESTINO]\n";
  write(STDOUT_FILENO, usage.c_str(), usage.length());
}

bool is_command_available(const std::string& command) {
  std::string path = get_environment_variable("PATH");
  std::string ruta;
  for (int i{0}; i < path.size(); ++i) {
    if (path[i] == ':') {
      ruta += "/" + command;
      if (access(ruta.c_str(), X_OK) != 0) {
        ruta.clear();
      } else {
          return true;
      }
    } else {
        ruta += path[i];
    }
  }
  return false;
}

int main(int argc, char* argv[]) {
  auto args = parse_arguments(argc, argv);
  if (!args.has_value()) {
    ParseArgsErrors error = args.error();
    std::string errores;
    if (error == ParseArgsErrors::multiple_compression_options) {
      errores = "backup-server: error: solo se admite una opción de compresión\n";
      write(STDERR_FILENO, errores.c_str(), errores.length());
      write_usage();
      return EXIT_FAILURE;
    } else if (error == ParseArgsErrors::too_many_arguments) {
        errores = "backup-server: error: hay demasiados argumentos\n";
        write(STDERR_FILENO, errores.c_str(), errores.length());
        write_usage();
        return EXIT_FAILURE;
    } else if (error == ParseArgsErrors::unknown_option) {
        errores = "backup-server: error: opción desconocida\n";
        write(STDERR_FILENO, errores.c_str(), errores.length());
        write_usage();
        return EXIT_FAILURE;
    }
  }
  ServerOptions options = args.value();
  std::string compresion = "backup-server: compresión: ";
  if (options.compression == ServerOptions::CompressionType::BZIP2) {
    if (!is_command_available("bzip2")) {
      std::string error = "backup-server: error: bzip2 no está instalado\n";
      write(STDERR_FILENO, error.c_str(), error.length());
      return EXIT_FAILURE;
    } else {
        compresion += "bzip2\n";
    }
  } else if (options.compression == ServerOptions::CompressionType::GZIP) {
      if (!is_command_available("gzip")) {
        std::string error = "backup-server: error: gzip no está instalado\n";
        write(STDERR_FILENO, error.c_str(), error.length());
        return EXIT_FAILURE;
      } else {
          compresion += "gzip\n";
      }
  } else if (options.compression == ServerOptions::CompressionType::XZ) {
      if (!is_command_available("xz")) {
        std::string error = "backup-server: error: xz no está instalado\n";
        write(STDERR_FILENO, error.c_str(), error.length());
        return EXIT_FAILURE;
      } else {
          compresion += "xz\n";
      }
  }
  
  write(STDOUT_FILENO, compresion.c_str(), compresion.length());

  std::string var_env = get_work_dir();
  if (var_env.empty()) {
    const char* no_existe = "La variable de entorno BACKUP_WORK_DIR no está definida.\n";
    write(STDERR_FILENO, no_existe, strlen(no_existe));
    return EXIT_FAILURE;
  }
  if (!directorio_correcto(var_env.c_str())) {
    return EXIT_FAILURE;
  }

  if (!directorio_correcto(options.destino_backup.c_str())) {
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
  std::string esperando = "backup-server: esperando solicitudes de backup en ";
  esperando += options.destino_backup + '\n';
  write(STDOUT_FILENO, esperando.c_str(), esperando.length());
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
  run_server(fifo_abierto, options.destino_backup, options);
  return EXIT_SUCCESS;
}