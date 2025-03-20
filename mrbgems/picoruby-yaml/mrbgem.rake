MRuby::Gem::Specification.new('picoruby-yaml') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'YAML parser for PicoRuby'

  if build.posix?
    spec.add_dependency 'picoruby-io'
  else
    spec.add_dependency 'picoruby-filesystem-fat'
    spec.add_dependency 'picoruby-vfs'
  end
end

