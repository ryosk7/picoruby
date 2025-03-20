class BLE
  class AdvertisingData
    def initialize
      @data = ""
    end

    attr_reader :data

    def add(type, *values)
      length_pos = @data.length
      @data << 0.chr # dummy length
      @data << type.chr
      length = 1
      values.each do |d|
        case d
        when String
          @data << d
          length += d.length
        when 0
          @data << "\x00"
          length += 1
        when Integer
          while 0 < d
            @data << (d & 0xff).chr
            d >>= 8
            length += 1
          end
        when nil
          @data << "\x00"
          length += 1
        else
          raise ArgumentError, "invalid data type: `#{d}`"
        end
      end
      @data[length_pos] = length.chr
    end

    def self.build(&block)
      instance = self.new
      block.call(instance)
      adv_data = instance.data
      if 31 < adv_data.length
        raise ArgumentError, "too long AdvData: (#{adv_data.length} bytes). It must be less than 32 bytes."
      end
      adv_data
    end
  end
end
