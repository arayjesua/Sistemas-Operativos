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
std::atomic<bool> quit_requested{false};

std::string get_environment_variable(const std::string& name) {
  char* value = getenv(name.c_str());
  if (value) {
    return std::string(value);
  } else {
      return std::string();
  }
}

int open_file(const std::string& file) {
  int abierto = open(file.c_str(), O_RDONLY);
  if (abierto == -1) {
    const char* error = "Error al abrir el archivo: ";
    if (errno == ENOENT) {
      const char* no_existe = "El archivo no existe\n";
      write(STDERR_FILENO, no_existe, strlen(no_existe));
    } else if (errno == EACCES) {
        const char* permisos = "Faltan permisos\n";
        write(STDERR_FILENO, permisos, strlen(permisos));
    } else if (errno == EISDIR) {
        const char* directorio = "No es un archivo. Es un directorio\n";
        write(STDERR_FILENO, directorio, strlen(directorio));
    } else {
        const char* error = "Error no reconocido\n";
        write(STDERR_FILENO, error, strlen(error));
    }
  } else {
      const char* abierto = "El archivo fue abierto con éxito\n";
      write(STDOUT_FILENO, abierto, strlen(abierto));
  }
  return abierto;
}

bool proceso_existe(const std::string& pid) {
  pid_t server = std::stoi(pid);
  if (kill(server, 0) == -1) {
    if (errno == ESRCH) {
      return false;
    }
  }
  return true;
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

int crear_fifo(const std::string& path) {
  int tuberia = mkfifo(path.c_str(), 0666);
  if (tuberia == -1) {
    const char* error = "ERROR: ";
    write(STDERR_FILENO, error, strlen(error));
    if (errno == EEXIST) {
      const char* error = "Ya existe un FIFO con ese nombre\nEliminando y creando de nuevo\n";
      write(STDERR_FILENO, error, strlen(error));
      int tuberia_recreada = unlink(path.c_str());
      if (tuberia_recreada == -1) {
        const char* error = "ERROR: No se pudo volver a crear la tubería\n";
        write(STDERR_FILENO, error, strlen(error));
        return tuberia_recreada;
      } else {
          return crear_fifo(path);
      }
    } else if (errno == EACCES) {
        const char* error = "No hay permisos para crear el archivo en el directorio indicado\n";
        write(STDERR_FILENO, error, strlen(error));
        return tuberia;
    } else if (errno == ENOENT) {
        const char* error = "Algún componente de la ruta no existe\n";
        write(STDERR_FILENO, error, strlen(error));
        return tuberia;
    } else {
        const char* error = "Error en la creación del FIFO\n";
        write(STDERR_FILENO, error, strlen(error));
        return tuberia;
    }
  }
  return tuberia;
}

int manejo_señales(sigset_t& conjunto) {
  int señal = sigemptyset(&conjunto);
  if (señal == -1) {
    const char* error = "ERROR: No es una señal válida\n";
    write(STDERR_FILENO, error, strlen(error));
    return señal;
  }
  señal = sigaddset(&conjunto, SIGUSR1);
  if (señal == -1) {
    const char* error = "Error al añadir SIGUSR1 al set\n";
    write(STDERR_FILENO, error, strlen(error));
    return señal;
  }
  señal = sigaddset(&conjunto, SIGINT);
  if (señal == -1) {
    const char* error = "Error al añadir SIGINT al set\n";
    write(STDERR_FILENO, error, strlen(error));
    return señal;
  }
  señal = sigaddset(&conjunto, SIGTERM);
  if (señal == -1) {
    const char* error = "Error al añadir SIGTERM al set\n";
    write(STDERR_FILENO, error, strlen(error));
    return señal;
  }
  señal = sigaddset(&conjunto, SIGQUIT);
  if (señal == -1) {
    const char* error = "Error al añadir SIGQUIT al set\n";
    write(STDERR_FILENO, error, strlen(error));
    return señal;
  }
  señal = sigaddset(&conjunto, SIGHUP);
  if (señal == -1) {
    const char* error = "Error al añadir SIGHUP al set\n";
    write(STDERR_FILENO, error, strlen(error));
    return señal;
  }
  señal = sigprocmask(SIG_BLOCK, &conjunto, NULL);
  if (señal == -1) {
    const char* error = "Error al bloquear las señales\n";
    write(STDERR_FILENO, error, strlen(error));
    return señal;
  }
  return señal;
}

std::string read_path_from_fifo(int fifo_fd) {
  const int max_size = 4096;
  std::string path(max_size, '\0');
  int tamaño = read(fifo_fd, path.data(), max_size);
  if (tamaño == -1) {
    return std::string();
  }
  path.resize(tamaño);
  return path;
}

std::string get_filename(const std::string& path) {
  char* path_copy = new char[path.length() + 1];
  path.copy(path_copy, path.size(), 0);
  std::string origen{basename(path_copy)};
  delete[] path_copy;
  return origen;
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
  while ((tamaño = read(origen, buffer.data(), buffer_size)) > 0) {
    write(destino, buffer.data(), tamaño);
  }
  close(origen);
  close(destino);
  return {};
}


int main(int argc, char* argv[]) {
  std::string var_env = get_environment_variable("BACKUP_WORK_DIR");
  if (var_env.empty()) {
    const char* no_existe = "La variable de entorno BACKUP_WORK_DIR no está definida.\n";
    write(STDERR_FILENO, no_existe, strlen(no_existe));
    return EXIT_FAILURE;
  }
  const char* directorio{};
  if (argc == 1) {
    directorio = ".";
  } else {
      directorio = argv[1];
  }
  if (!directorio_correcto(directorio)) {
    return EXIT_FAILURE;
  }
  std::string ruta_pid = var_env + "/backup-server.pid";
  std::string pid(buffer_size, '\0');
  int archivo_pid = open_file(ruta_pid.c_str());
  if (archivo_pid != -1) {
    int tamaño = read(archivo_pid, pid.data(), buffer_size);
    pid.resize(tamaño);
    close(archivo_pid);
  } else {
      return EXIT_FAILURE;
  }
  if (!pid.empty()) {
    if (proceso_existe(pid)) {
      const char* existe = "El proceso ya existe.\n";
      write(STDERR_FILENO, existe, strlen(existe));
      return EXIT_FAILURE;
    } else {
        const char* no_existe = "El proceso no existe.\n";
        write(STDOUT_FILENO, no_existe, strlen(no_existe));
    }
  }
  std::string fifo{var_env + "/backup.fifo"};
  if (crear_fifo(fifo) != -1) {
    ruta_pid = var_env + "/backup-server.pid";
    int archivo_pid = open(ruta_pid.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (archivo_pid != -1) {
      pid_t pid_pros = getpid();
      std::string pid_process;
      pid_process += std::to_string(pid_pros) + "\n";
      write(archivo_pid, pid_process.data(), pid_process.length());
      close(archivo_pid);
    } else {
        return EXIT_FAILURE;
    }
  }
  sigset_t conjunto;
  if (manejo_señales(conjunto) == -1) {
    return EXIT_FAILURE;
  }
  int fifo_abierto = open(fifo.c_str(), O_RDONLY);
  if (fifo_abierto == -1) {
    const char* error = "Error al abrir el FIFO\n";
    write(STDERR_FILENO, error, strlen(error));
    return EXIT_FAILURE;
  }
  // BUCLE PRINCIPAL
  int sig;
  while (!quit_requested) {
    int espera = sigwait(&conjunto, &sig);
    if (espera != 0) {
      const char* error = "Error al bloquear las señales\n";
      write(STDERR_FILENO, error, strlen(error));
      return EXIT_FAILURE;
    }
    if (sig == SIGUSR1) {
      std::string ruta_origen = read_path_from_fifo(fifo_abierto);
      if (ruta_origen.empty()) {
        close(fifo_abierto);
        fifo_abierto = open(fifo.c_str(), O_RDONLY);
        continue;
      }
      std::string archivo = get_filename(ruta_origen);
      std::string destino_backup = std::string(directorio) + "/" + archivo;
      auto copia = copy_file(ruta_origen, destino_backup, 0666);
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
    } else if (sig == SIGINT || sig == SIGTERM || sig == SIGHUP || sig == SIGQUIT) {
        quit_requested = true;
    }
  }
  close(fifo_abierto);
  std::string pid_path = var_env +"/backup-server.pid";
  if (unlink(pid_path.c_str()) != -1) {
    const char* exito = "ARCHIVO PID BORRADO CON ÉXITO\n";
    write (STDOUT_FILENO, exito, std::strlen(exito));
  } else {
      const char* exito = "Error al borrar el archivo PID\n";
      write (STDERR_FILENO, exito, std::strlen(exito));
  }

  if (unlink(fifo.c_str()) != -1) {
    const char* exito = "ARCHIVO FIFO BORRADO CON ÉXITO\n";
    write (STDOUT_FILENO, exito, std::strlen(exito));
  } else {
      const char* exito = "Error al borrar el archivo FIFO\n";
      write (STDERR_FILENO, exito, std::strlen(exito));
  }
  return EXIT_SUCCESS;
}
