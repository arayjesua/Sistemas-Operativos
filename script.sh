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
  echo
  uname -a
  echo
}


show_uptime()
{
  echo "${TEXT_ULINE}Tiempo de encendido del sistema$TEXT_RESET"
  echo
  uptime
  echo
}


drive_space()
{
  echo "${TEXT_ULINE}Espacio ocupado en particiones/discos duros del sistema $TEXT_RESET"
  echo
  df -a -B1 | tr -s ' ' | cut -d' ' -f3 | tail -n +2 | awk 'BEGIN {usado=0} {usado+=$1} END {printf("Espacio usado: %d Bytes\n", usado)}'
  echo
}


home_space()
{
  if [ "$USER" == root ]; then
    echo
    du --max-depth=1 /home | head -n -1 | sed 's|/.*/||' | sort -gr | awk 'BEGIN {print("USADO\tDIRECTORIO\n")} {print $0}'
  else
    du --max-depth=1 $HOME | head -n -1 | sed 's|/.*/||' | sort -gr | awk 'BEGIN {print("USADO\tDIRECTORIO\n")} {print $0}'
  fi
  echo
}

##### Programa principal

cat << _EOF_
$TEXT_BOLD$TITLE$TEXT_RESET

$TEXT_GREEN$TIME_STAMP$TEXT_RESET
_EOF_

system_info
show_uptime
drive_space
home_space
