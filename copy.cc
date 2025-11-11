#include <sys/stat.h>
#include <iostream>
#include <expected>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <system_error>
#include <vector>

const int buffer_size = 65536;


bool check_args(int argc, char* argv[]) {
  if (argc != 3) {
    const char* error = "ERROR: Debe especificar ORIGEN y DESTINO\n";
    write(STDERR_FILENO, error, std::strlen(error));
    return false;
  }
  struct stat stat1;
  struct stat stat2;
  if (stat(argv[1], &stat1) != 0) {
    const char* error = "ERROR: No se pudieron cargar los datos de ORIGEN\n";
    write(STDERR_FILENO, error, std::strlen(error));
    return false;
  }
  if (stat(argv[2], &stat2) == 0) {
    if (stat1.st_dev == stat2.st_dev && stat1.st_ino == stat2.st_ino) {
      const char* error = "ERROR: Los archivos ORIGEN y DESTINO son el mismo\n";
      write(STDERR_FILENO, error, std::strlen(error));
      return false;
    }
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

std::expected<void, std::system_error> copy_file(const std::string& src_path, const std::string& dest_path, mode_t dst_perms=0) {
  int origen = open(src_path.c_str(), O_RDONLY);
  if (origen == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al abrir el archivo de origen"));
  }
  int destino = open(dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, dst_perms);
  if (destino == -1) {
    return std::unexpected(std::system_error(errno, std::system_category(), "Error al abrir el archivo de destino"));
  }
  std::vector<char> buffer(buffer_size);
  while (read(origen, buffer.data(), buffer_size)) {
    write(destino, buffer.data(), buffer_size);
  }
  close(origen);
  close(destino);
  return {};
}

std::string get_filename(const std::string& path) {
  char* path_copy = new char[path.length() + 1];
  path.copy(path_copy, path.size(), 0);
  std::string origen{basename(path_copy)};
  delete[] path_copy;
  return origen;
}

int main(int argc, char* argv[]) {
  if(!check_args(argc, argv)) {
    return EXIT_FAILURE;
  }    
  std::string origen{argv[1]};
  std::string destino{argv[2]};
  if (is_directory(destino)) {
    destino += ("/" + get_filename(origen));
  }
  struct stat stat1;
  struct stat stat2;
  if (stat(origen.c_str(), &stat1) != 0) {
    const char* error = "ERROR: No se pudieron cargar los datos de ORIGEN\n";
    write(STDERR_FILENO, error, std::strlen(error));
    return false;
  }
  if (stat(destino.c_str(), &stat2) == 0) {
    if (stat1.st_dev == stat2.st_dev && stat1.st_ino == stat2.st_ino) {
      const char* error = "ERROR: Los archivos ORIGEN y DESTINO son el mismo\n";
      write(STDERR_FILENO, error, std::strlen(error));
      return false;
    }
  }
  auto copia = copy_file(origen, destino, 0666);
  if (copia.has_value()) {
    const char* exito = "ARCHIVO COPIADO CON ÉXITO\n";
    write (STDOUT_FILENO, exito, std::strlen(exito));
  } else {
      std::system_error error = copia.error();
      std::string msg_error{error.what()};
      msg_error += "\n";
      write(STDERR_FILENO, msg_error.c_str(), msg_error.length());
  }
  return EXIT_SUCCESS;
}