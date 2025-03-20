require 'time'

class File < IO
  attr_accessor :path

  def self.new(fd_or_path, mode = "r", perm = 0666, &block)
    if fd_or_path.is_a? Integer
      super(fd_or_path, mode, &block)
    else
      fd = IO.sysopen(fd_or_path, mode, perm)
      instance = super(fd, mode, perm, &block)
      instance.path = fd_or_path
      instance
    end
  end

  # Alternative to File.read
  def self.load_file(path, length = nil, offset = nil)
    File.open(path) do |f|
      f.seek(offset) if offset
      f.read(length)
    end
  end

  def expand(_size)
    # no-op. For compatibility with FAT filesystems.
    size
  end

  def atime
    t = self._atime
    t && Time.at(t)
  end

  def ctime
    t = self._ctime
    t && Time.at(t)
  end

  def mtime
    t = self._mtime
    t && Time.at(t)
  end

  def inspect
    "<#{self.class}:#{@path}>"
  end

  def self.join(*names)
    return "" if names.empty?

    names = names.map do |name|
      case name
      when String
        name
      when Array
        if names == name
          raise ArgumentError, "recursive array"
        end
        join(name)
      else
        raise TypeError, "no implicit conversion of #{name.class} into String"
      end
    end

    return names[0] if names.size == 1

    if names[0][-1] == File::SEPARATOR
      s = names[0][0, names[0].size - 1]
    else
      s = names[0].dup
    end

    s = s.to_s

    (1..names.size-2).each { |i|
      t = names[i]
      if t[0] == File::SEPARATOR and t[-1] == File::SEPARATOR
        t = t[1, t.size - 2]
      elsif t[0] == File::SEPARATOR
        t = t[1, t.size - 1]
      elsif t[-1] == File::SEPARATOR
        t = t[0, t.size - 1]
      end
      t = t.to_s
      s += File::SEPARATOR + t if t != ""
    }
    if names[-1][0] == File::SEPARATOR
      s += File::SEPARATOR + names[-1][1, names[-1].size - 1].to_s
    else
      s += File::SEPARATOR + names[-1]
    end
    s
  end

  def self._concat_path(path, base_path)
    if path[0] == "/" || path[1] == ':' # Windows root!
      expanded_path = path
    elsif path[0] == "~"
      if (path[1] == "/" || path[1] == nil)
        dir = path[1, path.size]
        home_dir = _gethome

        unless home_dir
          raise ArgumentError, "couldn't find HOME environment -- expanding '~'"
        end

        expanded_path = home_dir
        expanded_path += dir if dir
        expanded_path += "/"
      else
        splitted_path = path.split("/")
        user = splitted_path[0][1, splitted_path[0].size]
        dir = "/" + splitted_path[1, splitted_path.size]&.join("/").to_s

        home_dir = _gethome(user)

        unless home_dir
          raise ArgumentError, "user #{user} doesn't exist"
        end

        expanded_path = home_dir
        expanded_path += dir if dir
        expanded_path += "/"
      end
    else
      expanded_path = _concat_path(base_path, _getwd)
      expanded_path += "/" + path
    end

    expanded_path
  end

  def self.expand_path(path, default_dir = '.')
    expanded_path = _concat_path(path, default_dir)
    drive_prefix = ""
    if File::ALT_SEPARATOR && expanded_path.size > 2 && ("A".."Z")===(expanded_path[0]&.upcase) && expanded_path[1] == ":"
      drive_prefix = expanded_path[0, 2].to_s
      expanded_path = expanded_path[2, expanded_path.size]
    end
    expand_path_array = []
    if File::ALT_SEPARATOR && expanded_path&.include?(File::ALT_SEPARATOR)
      expand_path = expanded_path.gsub(File::ALT_SEPARATOR, '/')
    end
    while expanded_path&.include?('//')
      expanded_path = expanded_path.gsub('//', '/')
    end

    if expanded_path != "/"
      expanded_path&.split('/')&.each do |path_token|
        if path_token == '..'
          if expand_path_array.size > 1
            expand_path_array.pop
          end
        elsif path_token == '.'
          # nothing to do.
        else
          expand_path_array << path_token
        end
      end

      expanded_path = expand_path_array.join("/")
      if expanded_path.empty?
        expanded_path = '/'
      end
    end
    if drive_prefix.empty?
      expanded_path.to_s
    else
      drive_prefix + expanded_path&.gsub("/", File::ALT_SEPARATOR).to_s
    end
  end

  def self.foreach(file)
    if block_given?
      self.open(file) do |f|
        f.each {|l| yield l}
      end
    else
      return self.new(file)
    end
  end

  def self.directory?(file)
    FileTest.directory?(file)
  end

  def self.exist?(file)
    FileTest.exist?(file)
  end

  def self.file?(file)
    FileTest.file?(file)
  end

  def self.pipe?(file)
    FileTest.pipe?(file)
  end

  def self.size(file = nil)
    if (self.instance_of? self.class)
      _size
    else
      if file.nil?
        raise ArgumentError, "missing filename"
      end
      FileTest.size(file)
    end
  end

  def self.size?(file)
    FileTest.size?(file)
  end

  def self.socket?(file)
    FileTest.socket?(file)
  end

  def self.symlink?(file)
    FileTest.symlink?(file)
  end

  def self.zero?(file)
    FileTest.zero?(file)
  end

  def self.extname(filename)
    fname = self.basename(filename)
    epos = fname.rindex('.')
    return '' if epos == 0 || epos.nil?
    return fname[epos, fname.size - epos]
  end

  def self.path(filename = nil)
    if self.instance_of?(self.class)
      @path
    else
      raise ArgumentError, "missing filename" if filename.nil?
      if filename.is_a?(String)
        filename
      else
        raise TypeError, "no implicit conversion of #{filename.class} into String"
      end
    end
  end
end
