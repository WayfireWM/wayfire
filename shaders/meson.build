# Thanks to wlroots' implementation
shaders = [
  'core-basic.vert',
  'core-basic.frag',
  'wobbly.vert',
  'wobbly.frag',
]

frag_common = [ 'color-transform.frag' ]

glslang = find_program('glslang', 'glslangValidator', native: true, required: true)
glslang_version_info = run_command(glslang, '--version', check: true).stdout()
glslang_version = glslang_version_info.split('\n')[0].split(':')[-1]

vulkan_shaders = []
foreach shader : shaders
	name = shader.underscorify() + '_data'
	args = [glslang, '-V', '@INPUT@', '-o', '@OUTPUT@', '--vn', name]
	if glslang_version.version_compare('>=11.0.0')
		args += '--quiet'
	endif

  deps = []
  if shader.contains('frag')
    deps += frag_common
  endif

	header = custom_target(
		shader + '_spv',
		output: shader + '.h',
		input: shader,
		command: args,
    depend_files: deps)

	vulkan_shaders += [header]
endforeach
