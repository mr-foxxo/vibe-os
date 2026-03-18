MRuby::Build.new do |conf|
  # Vibe-OS i686-elf cross-compile configuration
  
  # Define cross-compiler toolchain
  toolchain :gcc
  
  # C compiler for i686-elf with Vibe-OS headers
  conf.cc do |cc|
    cc.command = 'i686-elf-gcc'
    cc.flags = %w(-m32 -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -Wall -Wextra)
    cc.include_paths = [
      "#{File.expand_path('../../../..', __FILE__)}/headers",
      "#{File.expand_path('../../../..', __FILE__)}/headers/lang",
      "/usr/local/i686-elf/include"
    ]
    cc.compile_options = %Q[%{flags} -MMD -o "%{outfile}" -c "%{infile}"]
  end

  # Archiver settings
  conf.archiver do |archiver|
    archiver.command = 'i686-elf-ar'
    archiver.archive_options = 'rs "%{outfile}" %{objs}'
  end

  # Linker settings (static library only)
  conf.linker do |linker|
    linker.command = 'i686-elf-ld'
    linker.flags = %w(-m elf_i386)
  end

  # Use default gems
  conf.gembox 'default'
  
  # Enable for embedded system
  conf.enable_bintest
  conf.enable_test
end
