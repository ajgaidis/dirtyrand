#!/bin/bash

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"

# Client variables
CLIENT_NAME="client"
CLIENT_BR_IP="192.168.1.1"
CLIENT_BR="${CLIENT_NAME}_br"
CLIENT_TAP="${CLIENT_NAME}_tap"
CLIENT_VM_IP="192.168.1.2"
CLIENT_VM_IF="enp0s3"

# Server variables
SERVER_NAME="server"
SERVER_BR_IP="10.0.0.1"
SERVER_BR="${SERVER_NAME}_br"
SERVER_TAP="${SERVER_NAME}_tap"
SERVER_VM_IP="10.0.0.2"
SERVER_VM_IF="enp0s3"

# Off-path attacker variables
ATTACKER_NAME="attacker"
ATTACKER_BR_IP="172.16.0.1"
ATTACKER_BR="${ATTACKER_NAME}_br"
ATTACKER_TAP="${ATTACKER_NAME}_tap"
ATTACKER_VM_IP="172.16.0.2"
ATTACKER_VM_IF="enp0s3"


setup_tap_bridge()
{
  local bridge_name="${1}"
  local bridge_ip="${2}"
  local tap_name="${3}"

  # Create bridge and assign an IP to it
  sudo ip link add name "${bridge_name}" type bridge
  sudo ip addr add "${bridge_ip}"/24 dev "${bridge_name}"
  sudo ip link set dev "${bridge_name}" up

  # Create TAP
  sudo ip tuntap add dev "${tap_name}" mode tap user $(whoami)
  sudo ip link set dev "${tap_name}" up

  # Attach TAP to bridge
  sudo ip link set dev "${tap_name}" master "${bridge_name}"
}

build_rootfs()
{
  local name="${1}"
  local static_ip="${2}"
  local gateway_ip="${3}"
  local interface="${4}"
  local other_pkgs=""

  mkdir -p "${SCRIPT_DIR}/rootfs-${name}"
  cd "${SCRIPT_DIR}/rootfs-${name}"

  if [[ "${name}" == "${SERVER_NAME}" ]]; then
    other_pkgs=" nginx-light"
  fi
  if [[ "${name}" == "${CLIENT_NAME}" ]]; then
    other_pkgs=" libsystemd-dev libsystemd-shared libsystemd0"
    #systemd-resolved 
  fi

  local pkgs="make
              git
              vim
              openssl
              python3
              build-essential
              xz-utils
              patchelf
              file
              tcpdump
              iputils-ping
              iputils-tracepath
              iproute2
              netcat-traditional
              locales
              xterm
              ${other_pkgs}"
  ADD_PACKAGE="$(echo ${pkgs} | sed 's/ /,/g')" \
      ../create-image.sh \
        -a x86_64 \
        -d bookworm \
        -f full \
        -s 20000 \
        --host "${name}" \
        --static-ip "${static_ip}" \
        --gateway-ip "${gateway_ip}" \
        --interface "${interface}"

  # mount the rootfs image
  mkdir -p mnt
  sudo mount -o loop ./bookworm.img ./mnt 

  # copy the necessary stuff inside
  sudo mkdir -p ./mnt/root/exploits
  sudo rsync -avh --exclude="rootfs*" \
    ../../Makefile ../../primitives ../../networking \
    ./mnt/root/exploits

  # XXX
  # setup resolver 
  #if [[ "${name}" == "${CLIENT_NAME}" ]]; then
  #  sudo mkdir -p ./mnt/etc/systemd
  #  echo "DNS=8.8.8.8" | sudo tee -a ./mnt/etc/systemd/resolved.conf
  #  echo "Cache=yes" | sudo tee -a ./mnt/etc/systemd/resolved.conf
  #fi

  ## resize terminal by default
  #echo "resize" | sudo tee -a ./mnt/root/.bashrc

  # unmount the rootfs image
  sudo umount ./mnt
}

start_vm()
{
  local name="${1}"
  local tap_name="${2}"
  local mac_addr="${3}"
  local debug_port="${4}"

  local kernel_flags="root=/dev/sda rw console=ttyS0 quiet nokaslr"

  # FIXME!
  qemu-system-x86_64 \
    -kernel "${SCRIPT_DIR}/../../build/linux-6.12.11-defconfig-dbg/arch/x86/boot/bzImage" \
    -drive file="${SCRIPT_DIR}/rootfs-${name}/bookworm.img",index=0,media=disk,format=raw \
    -nographic \
    -append "${kernel_flags}" \
    -m 4G \
    -smp 1 \
    -accel tcg \
    -cpu max \
    -gdb tcp::"${debug_port}" \
    -netdev tap,id="${name}_net",ifname="${tap_name}",script=no,downscript=no \
    -device e1000,netdev="${name}_net",mac="${mac_addr}"
    #--enable-kvm \
  #  -cpu host \
    #-snapshot
}

print_usage()
{
    cat 1>&2 <<EOF
Usage: $(ps -o args= ${PPID} | cut -d' ' -f2) [options]

Options:
  TODO!
EOF
}

main()
{
    # check for command line arguments
    if [[ ${#} == 0 ]]; then
        print_usage
        exit 1
    fi

    case "${1}" in
      network)
        # Setup TAP/bridge for client and server
        setup_tap_bridge "${CLIENT_BR}" "${CLIENT_BR_IP}" "${CLIENT_TAP}"
        setup_tap_bridge "${SERVER_BR}" "${SERVER_BR_IP}" "${SERVER_TAP}"
        setup_tap_bridge "${ATTACKER_BR}" "${ATTACKER_BR_IP}" "${ATTACKER_TAP}"

        # Setup IP forwarding
        sudo sysctl -w net.ipv4.ip_forward=1
        ;;

      rootfs)
        build_rootfs "${CLIENT_NAME}" "${CLIENT_VM_IP}" "${CLIENT_BR_IP}" "${CLIENT_VM_IF}"
#        build_rootfs "${SERVER_NAME}" "${SERVER_VM_IP}" "${SERVER_BR_IP}" "${SERVER_VM_IF}"
#        build_rootfs "${ATTACKER_NAME}" "${ATTACKER_VM_IP}" "${ATTACKER_BR_IP}" "${ATTACKER_VM_IF}"
        ;;

      vm-client)
        start_vm "client" "${CLIENT_TAP}" "DE:AD:BE:EF:01:01" "1235"
        ;;

      vm-server)
        start_vm "server" "${SERVER_TAP}" "DE:AD:BE:EF:02:02" "1236"
        ;;

      vm-attacker)
        start_vm "attacker" "${ATTACKER_TAP}" "DE:AD:BE:EF:03:03" "1237"
        ;;

      help )
        print_usage
        ;;

      * )
        do_error "Invalid argument: ${1}."
        ;;
    esac
}
main ${@}
