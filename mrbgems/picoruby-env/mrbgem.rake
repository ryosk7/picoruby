MRuby::Gem::Specification.new('picoruby-env') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'ENV'

  # The nRF52 port persists to /etc/env through littlefs's C API.
  #
  # Include paths only, deliberately: add_dependency would close the cycle
  # env -> littlefs -> time -> env, because picoruby-time depends on this
  # gem for ENV_get_timezone_offset. The C functions link regardless -- both
  # gems are in the same binary -- and littlefs_ensure_mounted() needs no
  # gem initialisation of its own, so build order does not matter here.
  if build.platform?(:nrf52)
    spec.cc.include_paths << "#{MRUBY_ROOT}/mrbgems/picoruby-littlefs/include"
    spec.cc.include_paths << "#{MRUBY_ROOT}/mrbgems/picoruby-littlefs/lib/littlefs"
  end

  if build.picoruby?
    # Workaround:
    #   Locate picoruby-mruby at the (almost) top of gem_init.c
    #   to define Kernel#require earlier than other gems
    spec.add_dependency 'picoruby-mruby'
  end
end
