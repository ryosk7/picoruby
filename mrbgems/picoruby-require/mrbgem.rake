MRuby::Gem::Specification.new('picoruby-require') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'PicoRuby require gem'

  if cc.defines.flatten.any?{ _1.match? /\AMRBC_USE_HAL_POSIX(=\d+)?\z/ }
    # TODO: in Wasm, you may need to implement File class with File System Access API
    spec.add_dependency 'picoruby-io'
  else
    spec.add_dependency 'picoruby-vfs'
    spec.add_dependency 'picoruby-filesystem-fat'
  end
  spec.add_dependency 'picoruby-sandbox'

  mrbgems_dir = File.expand_path "..", build_dir

  # picogems to be required in Ruby
  picogems = Hash.new
  task :collect_gems => "#{mrbgems_dir}/gem_init.c" do
    build.gems.each do |gem|
      if gem.name.start_with?("picoruby-") && !gem.name.start_with?("picoruby-bin-")
        gem_name = gem.name.sub(/\Apicoruby-?/,'')
        mrbfile = "#{mrbgems_dir}/#{gem.name}/mrblib/#{gem_name}.c"
        src_dir = "#{gem.dir}/src"
        initializer = if Dir.exist?(src_dir) && !Dir.empty?(src_dir)
                        "mrbc_#{gem_name}_init".gsub('-','_')
                      else
                        "NULL"
                      end
        picogems[gem.require_name || File.basename(mrbfile, ".c")] = {
          mrbfile: mrbfile,
          initializer: initializer
        }
        rbfiles = Dir.glob("#{gem.dir}/mrblib/**/*.rb").sort
        file mrbfile => rbfiles do |t|
          next if t.prerequisites.empty?
          mkdir_p File.dirname(t.name)
          File.open(t.name, 'w') do |f|
            name = File.basename(t.name, ".c").gsub('-','_')
            mrbc.run(f, t.prerequisites, name, cdump: false)
            if initializer != "NULL"
              f.puts
              f.puts "void #{initializer}(mrbc_vm *vm);"
            end
          end
        end
      end
    end
  end

  build.libmruby_objs << objfile("#{mrbgems_dir}/picogem_init")
  file objfile("#{mrbgems_dir}/picogem_init") => ["#{mrbgems_dir}/picogem_init.c"]

  file "#{mrbgems_dir}/picogem_init.c" => [*picogems.values.map{_1[:mrbfile]}, MRUBY_CONFIG, __FILE__, :collect_gems] do |t|
    mkdir_p File.dirname t.name
    open(t.name, 'w+') do |f|
      f.puts <<~PICOGEM
        #include <stdio.h>
        #include <stdbool.h>
        #include <mrubyc.h>
      PICOGEM
      f.puts
      picogems.each do |_require_name, v|
        Rake::FileTask[v[:mrbfile]].invoke
        f.puts "#include \"#{v[:mrbfile]}\"" if File.exist?(v[:mrbfile])
      end
      f.puts
      f.puts <<~PICOGEM
        typedef struct picogems {
          const char *name;
          const uint8_t *mrb;
          void (*initializer)(mrbc_vm *vm);
          bool required;
        } picogems;
      PICOGEM
      f.puts
      f.puts "static picogems prebuilt_gems[] = {"
      picogems.each do |require_name, v|
        name = File.basename(v[:mrbfile], ".c")
        f.puts "  {\"#{require_name}\", #{name.gsub('-','_')}, #{v[:initializer]}, false}," if File.exist?(v[:mrbfile])
      end
      f.puts "  {NULL, NULL, NULL, true} /* sentinel */"
      f.puts "};"
      f.puts
      f.puts <<~PICOGEM
        /* public API */
        bool
        picoruby_load_model(const uint8_t *mrb)
        {
          mrbc_vm *vm = mrbc_vm_open(NULL);
          if (vm == 0) {
            console_printf("Error: Can't open VM.\\n");
            return false;
          }
          if (mrbc_load_mrb(vm, mrb) != 0) {
            console_printf("Error: %s\\n", vm->exception.exception->message);
            mrbc_vm_close(vm);
            return false;
          }
          mrbc_vm_begin(vm);
          mrbc_vm_run(vm);
          if (vm->exception.tt != MRBC_TT_NIL) {
            console_printf("Error: Exception occurred.\\n");
            mrbc_vm_end(vm);
            mrbc_vm_close(vm);
            return false;
          }
          mrbc_raw_free(vm);
          return true;
        }

        static int
        gem_index(const char *name)
        {
          if (!name) return -1;
          for (int i = 0; ; i++) {
            if (prebuilt_gems[i].name == NULL) {
              return -1;
            } else if (strcmp(name, prebuilt_gems[i].name) == 0) {
              return i;
            }
          }
        }

        bool
        picoruby_load_model_by_name(const char *gem)
        {
          int i = gem_index(gem);
          if (i < 0) return false;
          return picoruby_load_model(prebuilt_gems[i].mrb);
        }

        static void
        c_extern(mrbc_vm *vm, mrbc_value *v, int argc)
        {
          if (argc == 0 || 2 < argc) {
            mrbc_raise(vm, MRBC_CLASS(ArgumentError), "wrong number of arguments (expected 1..2)");
            return;
          }
          if (GET_TT_ARG(1) != MRBC_TT_STRING) {
            mrbc_raise(vm, MRBC_CLASS(TypeError), "wrong argument type");
            return;
          }
          const char *name = (const char *)GET_STRING_ARG(1);
          int i = gem_index(name);
          if (i < 0) {
            SET_NIL_RETURN();
            return;
          }
          bool force = false;
          if (argc == 2 && GET_TT_ARG(2) == MRBC_TT_TRUE) {
            force = true;
          }
          if ((force || !prebuilt_gems[i].required)) {
            if (prebuilt_gems[i].initializer) prebuilt_gems[i].initializer(vm);
            if (!picoruby_load_model(prebuilt_gems[i].mrb)) {
              SET_NIL_RETURN();
            } else {
              prebuilt_gems[i].required = true;
              SET_TRUE_RETURN();
            }
          } else {
            SET_FALSE_RETURN();
          }
        }
      PICOGEM
      f.puts

      f.puts <<~PICOGEM
        void
        picoruby_init_require(mrbc_vm *vm)
        {
          mrbc_define_method(vm, mrbc_class_object, "extern", c_extern);
          mrbc_value self = mrbc_instance_new(vm, mrbc_class_object, 0);
          mrbc_instance_call_initialize(vm, &self, 0);
          mrbc_value args[2];
          args[0] = self;
          args[1] = mrbc_string_new_cstr(vm, "require");
          c_extern(vm, args, 1);
          args[1] = mrbc_string_new_cstr(vm, "io");
          c_extern(vm, args, 1);
        }
      PICOGEM
    end
  end

end

