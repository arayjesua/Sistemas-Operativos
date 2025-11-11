#!/usr/bin/env bash

# sysinfo - Un script que informa del estado del sistema


##### Constantes


TITLE="Información del sistema para $HOSTNAME"
RIGHT_NOW=$(date +"%x %r%Z")
TIME_STAMP="Actualizada el $RIGHT_NOW por $USER"

##### Estilos

TEXT_BOLD=$'\x1b[1m'
TEXT_GREEN=$'\x1b[32m'
TEXT_RESET=$'\x1b[0m'



##### Funciones

system_info()
{
  echo "${TEXT_ULINE}Versión del sistema${TEXT_RESET}"
  uname -a
  echo
}


show_uptime()
{
  echo "${TEXT_ULINE}Tiempo de encendido del sistema$TEXT_RESET"
  uptime
  echo
}


drive_space()
{
  df -a -B1 | tr -s ' ' | cut -d' ' -f3 | tail -n +2 | awk 'BEGIN {usado=0} {usado+=$1} END {printf("Espacio usado: %d Bytes\n", usado)}'
  echo
}


home_space()
{
  if [ "$USER" == root ]; then
    echo
    usado=$(du --max-depth=1 /home | sed 's|/.*/||' | head -n 1 | awk '{print $1}')
    directorio=$(du --max-depth=1 /home | sed 's|/.*/||' | head -n 1 | awk '{print $2}')
    numero_dir=$(find /home -type d | wc -l)
    numero_archivos=$(find /home -type f | wc -l)
    printf "%10s\t%10s\t%10s\t%10s\n" "ARCHIVOS" "DIRECTORIOS" "USADO" "DIRECTORIO"
    printf "%10s\t%11s\t%10s\t%10s\n" "$numero_archivos" "$numero_dir" "$usado" "$directorio"
  else
    usado=$(du --max-depth=1 $HOME | sed 's|/.*/||' | tail -n 1 | awk '{print $1}')
    directorio=$(du --max-depth=1 $HOME | sed 's|/.*/||' | tail -n 1 | awk '{print $2}')
    numero_dir=$(find $HOME -type d | wc -l)
    numero_archivos=$(find $HOME -type f | wc -l)
    printf "%10s\t%10s\t%10s\t%10s\n" "ARCHIVOS" "DIRECTORIOS" "USADO" "DIRECTORIO"
    printf "%11s\t%11s\t%10s\t%10s\n" "$numero_archivos" "$numero_dir" "$usado" "$directorio" 
  fi
  echo
}


  PROGNAME=$(basename $0)

error_exit()
{
  echo "${PROGNAME}: ${1:-Error desconocido}" >&2
  exit 1
}

# number_process()
# {
#   ps -U root | wc -l
#   echo
# }  

# Opciones por defecto
interactive=
filename=~/sysinfo.txt


##### Programa principal

usage() 
{
  echo "usage: sysinfo [-f filename] [-i] [-h]"
}

write_page()
{
  cat << _EOF_
  $TEXT_BOLD$TITLE$TEXT_RESET

  $(system_info)
  $(show_uptime)
  $(drive_space)
  $(home_space)


  $TEXT_GREEN$TIME_STAMP$TEXT_RESET
_EOF_
}

interactive_mode() 
{
  if [ $interactive -eq 1 ]
    then
      if dialog  --title "Informe del sistema" --yesno "Mostrar el informe del sistema por pantalla (S/N): " 6 35
        then
          clear
          system_info
          show_uptime
          drive_space
          home_space
          exit 0
      else
        clear
        NOMBRE_ARCHIVO=$(dialog --title "Nombre archivo" --output-fd 1 --inputbox "Introduzca el nombre del archivo [~/sysinfo.txt]: " 6 35)
        if [ -z $NOMBRE_ARCHIVO ]
          then 
          if [ -e $filename ]
            then
              if dialog --title "Sobreescribir" --yesno "El archivo de destino existe.¿Sobreescribir? (S/N)" 6 35
               then
                write_page > $NOMBRE_ARCHIVO
              else
                write_page > $filename 
                clear
                exit 0
              fi
          fi
        elif [ -e $NOMBRE_ARCHIVO ]
          then
            if dialog --title "Sobreescribir" --yesno "El archivo de destino existe.¿Sobreescribir? (S/N)" 6 35
              then
                write_page > $NOMBRE_ARCHIVO
            else 
              clear
              exit 0
            fi
        else 
          write_page > $NOMBRE_ARCHIVO
        fi
        clear
        exit 0
      fi
  fi 
}

# Procesar la línea de comandos del script para leer las opciones
while [ "$1" != "" ]; do
    case $1 in
        -f | --file )
            shift
            filename=$1
            ;;
        -i | --interactive )
            interactive=1
            interactive_mode
            ;;
        -h | --help ) 
            usage
            exit
            ;;
        -fu )
            shift
            nombre_usuario=$1
            numero_procesos=$(ps -u $nombre_usuario | wc -l)
            proceso_mayor_cpu=$(ps -Af -u $nombre_usuario --no-header | tr -s ' ' | cut -d ' ' -f7,8 | sort -r | head -n +1)
            proceso_sin_tiempo=$(echo $proceso_mayor_cpu | tr -s ' ' | cut -d ' ' -f2)
            echo "El usuario $nombre_usuario ha ejecutado $numero_procesos procesos"
            echo "El proceso que más consume es $proceso_sin_tiempo"
            ;;
        * )
            error_exit "Opción $1 no es posible"
    esac
    shift
done

instalado=$(type -P df)
if [ $? -ne 0 ]
  then
    error_exit "$LINENO: El comando df no está instalado"
fi

instalado=$(type -P du)
if [ $? -ne 0 ]
  then
    error_exit "$LINENO: El comando du no está instalado"
fi

instalado=$(type -P uptime)
if [ $? -ne 0 ]
  then
    error_exit "$LINENO: El comando uptime no está instalado"
fi

# Generar el informe del sistema y guardarlo en el archivo indicado
# en $filename
write_page > $filename




