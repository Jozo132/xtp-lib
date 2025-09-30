// @ts-check
"use strict"

// ###############################################################################################
// Copyright (c) 2025, J.Vovk
//
// This is a simple web file compiler that compiles the source code of a web server into a single C/C++ header file.
// The header file is then included in the web server source code.
// The primary purpose of this tool is to make it easier to compile the whole web server into a single binary file.
// This script supports nested directories and links them accordingly.
// Usage:
//   node web-compile.js --input <input_directory> --output <output_file> --function <setup_function_name> [--build <build_directory>]
//   --input: the directory where the web server source code is located (default: 'src')
//   --output: the path and name of the output header file which will contain the complete compiled web server source code (default: './src_files.h')
//   --function: the name of the function that has to be called in the web server source code to initialize the web server files (default: 'route_files')
//   --build: the directory where the compiled files for SPIFFS will be stored (default: 'build')
// Example:
//   node web-compile.js --input ./src --output ./src_files.h --function route_files --build ./build
//
// ###############################################################################################

const { promisify } = require('util')
const path = require('path')
const fs = require("fs")
const readdir = promisify(fs.readdir)
const stat = promisify(fs.stat)
// const exec = promisify(require('child_process').exec)

/** @param { string[] } args */
const argParser = (args) => {
    /** @type { { [key: string]: any } } */
    const argv = {}
    for (let i = 0; i < args.length; i++) {
        if (args[i]) {
            if (args[i].startsWith('--')) { // parse: `--key value` or `--key` or `--key=value`
                const arg = args[i].substring(2)
                if (arg.includes('=')) {
                    const parts = arg.split('=')
                    const key = parts.shift() || ''
                    argv[key.toLowerCase()] = parts.join('=')
                } else if (args[i + 1] && !args[i + 1].startsWith('-')) {
                    const key = arg
                    argv[key.toLowerCase()] = args[++i]
                } else {
                    const key = arg
                    argv[key.toLowerCase()] = true
                }
            } else if (args[i] && args[i].startsWith('-') && (args[i + 1] || '').startsWith('-')) {
                const key = args[i].substring(1)
                argv[key.toLowerCase()] = true
            } else if (args[i] && args[i].startsWith('-')) {
                const key = args[i].substring(1)
                argv[key.toLowerCase()] = args[++i]
            }
        }
    }
    return argv
}


const argv = argParser(process.argv.slice(2))

// input directory: this is the directory where the web server source code is located
const input_directory = argv.i || argv.input || 'src'
const build_directory = argv.b || argv.build || 'build'

// output file: this is the path and name of the output header file which will contain the complete compiled web server source code
const output_file = argv.o || argv.output || './src_files.h'

// setup function: this is the name of the function that has to be called in the web server source code to initialize the web server files
const setup_function_name = argv.f || argv.function || 'route_files'

let file_index = 0
// ESP8266WebServer file linking: this is the method which will be executed when running the "setup_function_name". That will link all the files to be served on the web server
/** @param { string } name * @param { string | Buffer } input_data */
const generate_http_server_file_hosting = (name, input_data) => {
    // const uint32_t file_1_size = 10;
    // const uint8_t file_1 [file_1_size + 1] PROGMEM = { 0x00, 0x01, ... }; // Buffer
    // const uint8_t file_1 [file_1_size + 1] PROGMEM = "01..."; // String
    // REST_SERVE_FILE("Test file", file_1_size, file_1);
    const isString = typeof input_data === 'string'
    const size = `const int32_t __file_${file_index}_size = ${isString ? -1 : input_data.length};` // @ts-ignore
    const data = `const char __file_${file_index}[${isString ? '' : `__file_${file_index}_size + 1`}] PROGMEM = ${isString ? (input_data.startsWith('"') ? input_data : JSON.stringify(input_data)) : `{ ${input_data.join(', ')} }`};`
    const setup = `REST_SERVE_FILE("${name}", __file_${file_index}, __file_${file_index}_size);`
    file_index++;
    return { size, data, setup }
}

// ###############################################################################################
// ###############################################################################################

/** @type { (dir: string) => Promise<string[]>  } */
const getFiles = async dir => {
    const subdirs = await readdir(dir)
    const files = await Promise.all(subdirs.map(async subdir => {
        const res = path.resolve(dir, subdir)
        return (await stat(res)).isDirectory() ? getFiles(res) : res
    })) // @ts-ignore
    return files.reduce((a, f) => a.concat(f), [])
}

/** @type { (file: string, data: string | Buffer) => Promise<void> } */
const saveFile = async (file, data) => {
    // Create folder if it doesn't exist recursively
    const dir = path.dirname(file)
    if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true })
    // Write file
    fs.writeFileSync(file, data)
}

/** @type { (input: string) => string } */
const convertAbsolutePathToRelativePath = input => input.split('\\').join('/').split(__dirname.split('\\').join('/') + '/').join('')
/** @type { (input: string) => string } */
const getName = input => input.split('/').pop() || ''
/** @param { string } type */
const sourceType = type => {
    type = type.toLowerCase()
    return type === 'html' ? 'html' : type === 'js' ? 'javascript' : type === 'css' ? 'css' : 'plain'
}
/** @type { (input: string) => false | "HTML" | "JS" | "CSS" } */
const compressableType = type => {
    type = type.toLowerCase()
    return type === 'html' ? 'HTML' : type === 'js' ? 'JS' : type === 'css' ? 'CSS' : false
}

// @ts-ignore
var html_minifier = null // @ts-ignore
var js_minifier = null // @ts-ignore
var css_postcss = null // @ts-ignore
var css_autoprefixer = null // @ts-ignore
var css_cssnano = null

const compress = {
    /** @param { String } input * @returns { Promise<String> } */
    HTML: async input => {// @ts-ignore
        html_minifier = html_minifier || require('@minify-html/js')
        const cfg = html_minifier.createConfiguration({
            do_not_minify_doctype: true,
            keep_spaces_between_attributes: false,
            ensure_spec_compliant_unquoted_attribute_values: true,
            minify_css: true,
            minify_js: true,
        });
        const output = html_minifier.minify(input, cfg).toString()
        return JSON.stringify(output)
    },
    /** @param { String } input * @returns { Promise<String> } */
    JS: async input => { // @ts-ignore
        const { minify } = js_minifier ? { minify: js_minifier } : require("uglify-js") // @ts-ignore
        if (!js_minifier) js_minifier = minify
        const result = minify(input)
        if (result.error) throw ' Failed to minify the JavaScript file, falling back to the original file'
        return JSON.stringify(result.code)
    },
    /** @param { String } input * @returns { Promise<String> } */
    CSS: async input => { // @ts-ignore
        css_postcss = css_postcss || require('postcss') // @ts-ignore
        css_autoprefixer = css_autoprefixer || require('cssnano') //  @ts-ignore
        css_cssnano = css_cssnano || require('autoprefixer') //  @ts-ignore
        const result = await css_postcss([css_cssnano, css_autoprefixer]).process(input, { from: undefined })
        return JSON.stringify(result.css)
    },
}

const main = async () => {
    console.log(`-------------------------------------------------------------------------------`)
    console.log(`Building WEB components ...`)

    // Get a list of file names
    const absolute_file_paths = await getFiles(input_directory)
    const files = absolute_file_paths.map(convertAbsolutePathToRelativePath)

    // Prepare the C/C++ file array of strings
    const rows = []

    // Add the header
    rows.push(`// ###############################################################################################`)
    rows.push(`// This file was generated by the "web-compile.js" script`)
    rows.push(`// Copyright (c) 2024, J.Vovk`)
    rows.push(`//`)
    rows.push(`// Do not edit this file directly, all changes here will be lost`)
    rows.push(`// It will be automatically regenerated when rebuilding the project`)
    rows.push(`// To make changes to the files, you should edit the "src" directory inside the "www" folder and rebuild the project`)
    rows.push(`// For more information see the "compile.js" script located in the "www" directory of the project`)
    rows.push(`// ###############################################################################################`)
    rows.push(`// ###############################################################################################`)
    rows.push(``)

    const global_definitions = []
    const definition_linking = []
    const file_handling = []

    let total_size = 0
    let total_uncompressed_size = 0
    let largest_file_size = 0
    const results = []

    // Delete folder build_directory recursively if it exists
    if (fs.existsSync(`./${build_directory}`)) fs.rmSync(`./${build_directory}`, { recursive: true })

    console.log(`Processing ${files.length} files ...`)

    // Now we want to create a const char* for each file
    for (const file of files) {
        // Read the file
        const contents_raw = fs.readFileSync(file)
        const contents = contents_raw.toString()
        const target_file_path = path.resolve(file).split(path.resolve(input_directory)).join('').split('\\').join('/')
        const name = getName(file)
        const type = (name.split('.').pop() || '').toLocaleLowerCase()
        /** @type { String | Buffer } */
        let output = contents_raw
        let compress_type = compressableType(type)
        if (compress_type === 'HTML') compress_type = false // HTML compression is not reliable enough yet
        let compress_success = true
        if (compress_type) try { output = await compress[compress_type](contents) } catch (e) { compress_success = false }
        if (compress_type && !compress_success) output = contents
        const original = compress_success ? contents : output // @ts-ignore
        const file_size = output.length - 2 + file.length + 1 // @ts-ignore
        const original_size = compress_success ? original.length - 2 + file.length + 1 : file_size
        const rate = 100 - (file_size / original_size) * 100
        largest_file_size = file_size > largest_file_size ? file_size : largest_file_size
        total_size += file_size
        total_uncompressed_size += original_size
        results.push([target_file_path, file_size, !compress_type || compress_success]) // [file_path, file_size, success]
        global_definitions.push('')
        global_definitions.push(`// Expected size: ${(file_size + '').padStart(8, ' ')} bytes - ${target_file_path} ${compress_type ? `(minified ${compress_type} from ${original_size} bytes ${compress_success ? `-> reduced by ${(rate > 0 ? '-' : '+') + (rate).toFixed(1)}% )` : `failed to compress, please read the README file for information)`}` : ''}`)
        const { size, data, setup } = generate_http_server_file_hosting(target_file_path, output)

        // const uint32_t file_1_size = 10;
        // const uint8_t file_1 [file_1_size] PROGMEM = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0' };
        // REST_SERVE_FILE("Test file", file_1_size, file_1);

        global_definitions.push(size)
        global_definitions.push(data)
        definition_linking.push(`    ${setup}`)
        // Also store the file in the output directory for SPIFFS at "./build_directory/<file_path>"
        if (compress_type) await saveFile(`${build_directory}${target_file_path}`, output)
        // Copy original file to the output directory for SPIFFS at "./build_directory/<file_path>"
        if (!compress_type) {
            await saveFile(`${build_directory}${target_file_path}`, '')
            fs.copyFileSync(file, path.resolve(`${build_directory}${target_file_path}`))
        }
        // if (!compress_type) await exec(`cp "${file}" "./${build_directory}/${target_file_path}"`)
    }

    const total_rate = 100 - (total_size / total_uncompressed_size) * 100
    const size_len = largest_file_size.toString().length
    for (const [file, file_size, success] of results) console.log(` - [${file_size.toString().padStart(size_len, ' ')} bytes]: ${file} ${success ? '' : ' -> failed to compress, please read the README file for information'}`)

    rows.push(`// Expected total size:    ${total_size} bytes ${total_size !== total_uncompressed_size ? `compressed      ( ${total_uncompressed_size} bytes raw -> reduced by ${(total_rate > 0 ? '-' : '+') + (total_rate).toFixed(1)}% )` : ''}`)
    rows.push(``)
    rows.push(`#ifndef __www_output_h__`)
    rows.push(`#define __www_output_h__`)
    rows.push(``)

    for (let i = 0; i < global_definitions.length; i++) rows.push(global_definitions[i])
    rows.push('')
    rows.push(`void ${setup_function_name}() {`)
    for (let i = 0; i < definition_linking.length; i++) rows.push(definition_linking[i])
    rows.push(`}`)
    rows.push(``)
    rows.push(`#endif // __www_output_h__`)

    // Write the file
    const output = rows.join("\n")
    fs.writeFileSync(output_file, output)

    console.log(`Source files generated in "include/src_files.h" and to be initialized with '${setup_function_name}()'`)
    console.log(`Expected flash usage of the source files: [${total_size} bytes]`)
    console.log(`-------------------------------------------------------------------------------`)
}

main().catch(console.error)