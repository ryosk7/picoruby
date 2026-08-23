# Thread (IEEE 802.15.4) networking, via OpenThread.
#
# The module is not a class because the stack is not one: OpenThread is
# built here with OPENTHREAD_CONFIG_MULTIPLE_INSTANCE_ENABLE off, so
# there is exactly one instance per boot. That also makes this a drop-in
# peer of CYW43, which Network aliases the same way.
#
# Naming: this is deliberately not called Thread. In picoruby the
# concurrency primitive is Task -- Task::Queue is documented as the
# rename of CRuby's Thread::Queue -- and reusing the word for a radio
# protocol would undo that. Network::Thread exists as the role-name
# alias, in the same place Network::WiFi does.
module OpenThread
  class ConnectTimeout < StandardError; end

  ROLES = [:disabled, :detached, :child, :router, :leader]

  # 802.15.4 channels in the 2.4 GHz band.
  CHANNEL_MIN = 11
  CHANNEL_MAX = 26

  def self.init
    __init
  end

  # Install the Active Dataset.
  #
  # network_key is the only field a node genuinely needs -- everything
  # else can be discovered -- but supplying pan_id and channel skips an
  # MLE Announce scan across every channel, which is the difference
  # between attaching in a second and attaching in a minute.
  #
  # tlvs: takes a raw Active Dataset TLV string instead, for datasets
  # produced elsewhere (`ot-ctl dataset active -x`).
  def self.configure(network_key: nil, pan_id: nil, channel: nil, tlvs: nil)
    if tlvs
      __set_dataset_tlvs(tlvs)
      return true
    end
    unless network_key.is_a?(String) && network_key.bytesize == 16
      raise ArgumentError, "network_key must be a 16-byte String"
    end
    if channel && (channel < CHANNEL_MIN || CHANNEL_MAX < channel)
      raise ArgumentError, "channel must be #{CHANNEL_MIN}..#{CHANNEL_MAX}"
    end
    if pan_id && (pan_id < 0 || 0xFFFF < pan_id)
      raise ArgumentError, "pan_id must be 0..0xFFFF"
    end
    __set_dataset(network_key, pan_id || -1, channel || -1)
  end

  # Order matters and is not ours to choose: otThreadSetEnabled answers
  # OT_ERROR_INVALID_STATE unless the interface is already up.
  def self.start
    __ip6_enable(true)
    __thread_enable(true)
    true
  end

  def self.stop
    __thread_enable(false)
    __ip6_enable(false)
    true
  end

  def self.role
    ROLES[__role] || :disabled
  end

  def self.attached?
    r = __role
    r != 0 && r != 1     # neither disabled nor detached
  end

  # Block until this node has joined, or give up.
  #
  # Returns true, or raises ConnectTimeout -- the same shape as
  # CYW43.connect_timeout, so Network can present one interface.
  def self.wait_attached(timeout_ms: 30_000)
    waited = 0
    until attached?
      if waited >= timeout_ms
        raise ConnectTimeout, "not attached after #{timeout_ms} ms (role: #{role})"
      end
      sleep_ms 100
      waited += 100
    end
    true
  end

  # A UDP socket over the Thread mesh.
  #
  # The method set mirrors CRuby's UDPSocket so that programs written
  # against picoruby-socket move across unchanged, but the class is
  # deliberately NOT installed as a top-level UDPSocket: mruby/c's
  # define_class returns an existing class rather than refusing, so a
  # later `require "socket"` would graft lwIP's C methods onto this one
  # and reinterpret its data pointer.
  class UDP
    # recvfrom(maxlen) -> [data, [af, port, host, host]]
    #
    # Blocks the Task, not the CPU: sleep_ms parks it and lets the
    # scheduler run, which is also what pumps OpenThread.
    # Blocks the Task, not the CPU: sleep_ms parks it and lets the
    # scheduler run, which is also what pumps OpenThread. A closed
    # socket raises IOError from recvfrom_nonblock rather than looping
    # forever on a nil that can never become a datagram.
    def recvfrom(maxlen = 512, flags = 0)
      while true
        result = recvfrom_nonblock(maxlen)
        return result if result
        sleep_ms 10
      end
      raise IOError, "unreachable"
    end

    def read(maxlen = 512)
      data, _addr = recvfrom(maxlen)
      data
    end
  end
end
