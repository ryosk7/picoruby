# ifconfig - display network interface configuration

begin
  require 'network'
rescue LoadError
  puts "Network interface not available"
  return
end

# Network defines a link constant only for platforms that have one, so
# a board without Wi-Fi reaches here with Network::WiFi undefined and
# NameError is not what LoadError above was meant to catch.
wifi = begin
  Network::WiFi
rescue NameError
  nil
end

unless wifi
  puts "No Wi-Fi interface on this platform"
  return
end

unless Network::WiFi.initialized?
  puts "Network::WiFi not initialized"
  return
end

puts "Link: WiFi"

if Network::WiFi.link_connected?
  puts "  Status: UP (#{Network::WiFi.tcpip_link_status_name})"
  if Network::WiFi.dhcp_supplied?
    puts "  DHCP: Active"
    puts "  IP Address: #{Network::WiFi.ipv4_address || 'unassigned'}"
    puts "  Netmask:    #{Network::WiFi.ipv4_netmask || 'unassigned'}"
    puts "  Gateway:    #{Network::WiFi.ipv4_gateway || 'unassigned'}"
  else
    puts "  DHCP: Inactive"
  end
else
  puts "  Status: DOWN"
end
