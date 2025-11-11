#!/usr/bin/bash

# open_files - 

# Variables globales
help=
offline=
exit=
filtro=
opcion=
sort=
user=
OPEN_FILES_FOLDER=
PROGNAME=$(basename $0)
usuario=
##### FUNCIONES #####

error_exit()
{
  echo "${PROGNAME}: ${1:-Error desconocido}" >&2
  exit 1
}

offline() {
  USUARIOS_CONECTADOS=$(who | tr -s ' ' | cut -d ' ' -f1 | sort -u | tr -s '\n' ',' | sed 's/,$//')
  USUARIOS_NO_CONECTADOS=$(lsof -u ^$USUARIOS_CONECTADOS -F u | grep '^u' | sed 's/^u//' | sort -u | tr -s '\n' ' ' | sed 's/ $/\n/')
  printf "%20s\t%16s\t%24s\n" "USUARIO" "PID(más antigüo)" "Nº ARCHIVOS REG ABIERTOS"
  for i in $USUARIOS_NO_CONECTADOS 
    do
      USUARIO=$(lsof -u "$i" | tr -s ' ' | cut -d ' ' -f3 | tail -n +2 | sort -u)
      PID=$(ps -u "$i" -o pid,etimes | tail -n +2 | sort -n | head -n +1 | tr -s ' ' | cut -d ' ' -f2)
      if [ -n "$OPEN_FILES_FOLDER" ]
        then 
          ARCHIVOS=$(lsof -u "$i" -a +d "$OPEN_FILES_FOLDER" | grep REG | wc -l)
      else
        ARCHIVOS=$(lsof -u "$i" | grep REG | wc -l) 
      fi
      printf "%20s\t%16s\t%24s\n" "$USUARIO" "$PID" "$ARCHIVOS"
  done
}

help()
{
  echo "El comando se debe ejecutar de la siguiente manera: open_files [opcion]"
  echo "Las opciones posibles son: "
  echo "-h y --help para mostrar ayuda del comando"
  echo "-f si se quiere aplicar un filtro (expresión regular) para filtrar la salida en base a la última columna"
  echo "-o y --offline para mostrar los usuarios que no están conectados al sistema"
  echo "-u [usuario1, usuario2, ...] para mostrar información sólo de esos usuarios"
  echo "-s para mostrar la lista ordenada por el número de archivos abiertos"
}

buscar_en_offline()
{
  for i in $usuario
    do
      offline | grep $i
  done
}

información_usuarios_conectados()
{
  for i in $usuario
    do
      conectados+="$(who | grep "$i" | tr -s ' ' | cut -d ' ' -f1) "
  done
  for i in $conectados
    do
      USUARIO=$(lsof -u "$i" | tr -s ' ' | cut -d ' ' -f3 | tail -n +2 | sort -u)
      PID=$(ps -u "$i" -o pid,etimes | tail -n +2 | sort -n | head -n +1 | tr -s ' ' | cut -d ' ' -f2)
      if [ -n "$OPEN_FILES_FOLDER" ]
        then 
          ARCHIVOS=$(lsof -u "$i" -a +d "$OPEN_FILES_FOLDER" | grep REG | wc -l)
      else
        ARCHIVOS=$(lsof -u "$i" | grep REG | wc -l) 
      fi
      printf "%20s\t%16s\t%24s\n" "$USUARIO" "$PID" "$ARCHIVOS"
  done
}

gestionar_funciones()
{
  if [ "$help" = 1 ]
    then
      help
  fi
  
  if [[ "$offline" = 1  && "$sort" != 1 && "$user" != 1 ]]
    then
      if [ "$filtro" = 1 ]
        then
          usuarios_offline=$(offline | tail -n +2 | tr -s ' ' | cut -d ' ' -f2 | tr -s '\n' ' ')
          for i in $usuarios_offline
            do
              echo "$i: $(lsof -u "$i" | tr -s ' ' | cut -d ' ' -f9 | grep "$opcion" | wc -l)"
          done
      else 
        offline
      fi
  fi
  
  if [ "$user" = 1 ]
    then
      if [[ "$offline" = 1 && "$filtro" != 1 ]]
        then
          if [[ "$sort" != 1 ]] 
            then 
              encabezado=$(offline | head -n 1)
              echo "$encabezado"
              buscar_en_offline
          else
            encabezado=$(offline | head -n 1)
            echo "$encabezado"
            buscar_en_offline | sort -n
          fi
      elif [[ "$offline" = 1 && "$filtro" = 1 ]]
        then 
          usuarios_offline=$(buscar_en_offline | tr -s ' ' | cut -d ' ' -f2 | tr -s '\n' ' ')
          for i in $usuarios_offline
            do
              echo "$i: $(lsof -u "$i" | tr -s ' ' | cut -d ' ' -f9 | grep "$opcion" | wc -l)"
          done
      elif [[ "$offline" != 1 && "$filtro" != 1 ]]
        then
          if [ "$sort" = 1 ]
            then 
              encabezado=$(offline | head -n 1)
              echo "$encabezado" 
              información_usuarios_conectados | sort -n
          else 
            encabezado=$(offline | head -n 1)
            echo "$encabezado"
            información_usuarios_conectados
          fi
      fi
      if [[ "$filtro" = 1 && "$offline" != 1 ]] 
        then
          for i in $usuario
            do
              lsof -u "$i" | tr -s ' ' | cut -d ' ' -f9 | grep "$opcion" | wc -l
          done
      fi 
  fi
  
  if [[ "$filtro" = 1 && "$user" != 1 && "$offline" != 1 ]]
    then
      lsof | tr -s ' ' | cut -d ' ' -f9 | grep "$opcion" | wc -l
      # Preguntar si es para cualquier usuario
  fi
  
  if [ "$sort" = 1 ]
    then
      if [[ "$offline" = 1 && "$user" != 1 ]] 
        then
          encabezado=$(offline | head -n 1)
          ordenados=$(offline | tail -n +2 | sort -n)
          printf "%s\n%s\n" "$encabezado" "$ordenados"
      fi
  fi
}

##### PROGRAMA PRINCIPAL #####

open_files() {
  EXISTE_LSOF=$(type -P lsof)
  if [ $? -ne 0 ] 
    then
      error_exit "$LINENO: El comando lsof no está instalado"
  fi

  if [ $# -eq 0 ]; then
    printf "%20s\t%24s\t%16s\n" "UID" "PID(proceso más antiguo)" "NÚMERO ARCHIVOS REGULARES ABIERTOS"
    for i in $(who | tr -s ' ' | cut -d ' ' -f1 | sort -fu); do
      if [ -n "$OPEN_FILES_FOLDER" ]
        then 
          ARCHIVOS=$(lsof -u "$i" -a +d "$OPEN_FILES_FOLDER" | grep REG | wc -l)
      else
        ARCHIVOS=$(lsof -u "$i" | grep REG | wc -l) 
      fi
      PID=$(ps -u "$i" -o pid,etimes | tail -n +2 | sort -n | head -n +1 | tr -s ' ' | cut -d ' ' -f2)
      if [ "$PID" = "" ]
        then 
          PID="NA"
      fi
      printf "%20s\t%24s\t\t%26s\n" "$i" "$PID" "$ARCHIVOS"
    done
  else
    while [ "$1" != "" ]
      do
        case $1 in
              -h|--help ) 
                help=1   
  		          ;;
              -o|--offline ) 
                offline=1 
      		      ;;
      	      -u )
      	        user=1
                shift
                while [[ "$1" != "" && "$1" != -* ]]
                  do
                    usuario+="$1 "
                    shift
                done
                continue
      	        ;;
      	      -f )
      	        filtro=1
      	        shift
      	        opcion=$1
      	        ;;
      	      -s|--sort )
      	        sort=1
      	        ;;
               * )
                error_exit "$LINENO: Comando "$1" no encontrado"
        esac
        shift
      done
  fi
  
  if [ -n "$OPEN_FILES_FOLDER" ]  
    then
      if [[ ! -d "$OPEN_FILES_FOLDER" ]]
        then
          error_exit "$LINENO: OPEN_FILES_FOLDER lleva a un directorio inexistente"
      fi
  fi
  gestionar_funciones
  exit 0
}

open_files "$@"
