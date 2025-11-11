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

##### FUNCIONES #####

exit_error() {
  INSTALADO=$(which lsof)
  if [ $? -ne 0 ]; then
    echo "El comando 'lsof' no está instalado."
    echo "Instálelo con 'sudo apt install lsof'."
    exit 2
  else 
    echo "La opción $1 no se detecta"
    echo "Pruebe open_files -h ó open_files --help para más información"
    exit 1
  fi
}

offline() {
  for i in $(who | tr -s ' ' | cut -d ' ' -f1); do
    ARCHIVOS2=$(lsof -u ^"$i" | grep REG | wc -l)
    # Duda que preguntar, la lista es como la que se muestra si no se pasan parámetros?
    # Si sí, cómo consigo el usuario (UID)
    # Si no, cómo es¿?
    echo ": $ARCHIVOS2"
  done
}

##### PROGRAMA PRINCIPAL #####

open_files() {
  if [ $# -eq 0 ]; then
    for i in $(who | tr -s ' ' | cut -d ' ' -f1 | sort -f); do
      ARCHIVOS=$(lsof -u "$i" | grep REG | wc -l)
      PID=$(lsof -u "$i" -t | ps -o pid,etimes | tail -n +2 | head -n +1 | sort -rn | tr -s ' ' | cut -d ' ' -f2)
      echo "UID		PID(proceso más antiguo		NÚMERO ARCHIVOS REGULARES ABIERTOS"
      echo "$i		$PID				$ARCHIVOS"
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
      	        # Preguntar cómo puedo hacer para saber cuántos usuarios vienen detrás
      	        ;;
      	      -f )
      	        filtro=1
      	        shift
      	        opcion=$1
      	        shift
      	        ;;
      	      -s|--sort )
      	        sort=1
      	        ;;
               * )
                echo CACA
                exit 0
        esac
      done
  fi
  
  if [ "$help" = 1 ]
    then
      echo "El comando se debe ejecutar de la siguiente manera: open_files [opcion]"
      echo "Las opciones posibles son: "
      echo "-h y --help para mostrar ayuda del comando"
      echo "-f si se quiere aplicar un filtro (expresión regular) para filtrar la salida en base a la última columna"
      echo "-o y --offline para mostrar los usuarios que no están conectados al sistema"
      echo "-u [usuario1, usuario2, ...] para mostrar información sólo de esos usuarios"
      echo "-s para mostrar la lista ordenada por el número de archivos abiertos"
  fi
  
  if [ "$offline" = 1 ]
    then
      for i in $(who | tr -s ' ' | cut -d ' ' -f1); do
        ARCHIVOS2=$(lsof -u ^"$i" | grep REG | wc -l)
        # Duda que preguntar, la lista es como la que se muestra si no se pasan parámetros?
        # Si sí, cómo consigo el usuario (UID)
        # Si no, cómo es¿?
        echo ": $ARCHIVOS2"
      done
  fi
  
  if [ "$user" = 1 ]
    then
     echo "FUNCIÓN -U"
  fi
  
  if [ "$filtro" = 1 ]
    then
      lsof | tr -s ' ' | cut -d ' ' -f9 | grep "$opcion" | wc -l
      # Preguntar si es para cualquier usuario
  fi
  
  if [ "$sort" = 1 ]
    then
      echo "FUNCIÓN SORT"
  fi
  exit 0
}

open_files "$@"
