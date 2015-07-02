
        std::string ReadFile(const std::string& filename, bool want_exception) try
        {
            boost::iostreams::mapped_file region(filename, boost::iostreams::mapped_file::readonly);                
            return std::string(region.const_data(), region.const_data() + region.size());
        }
        catch (const std::exception& e)
        {
            // zero length file
            if (!want_exception && strcmp(e.what(), "failed mapping file: Invalid argument") == 0)
                return std::string();

            throw;
        }

        bool unzip(const std::string& source, std::string& destination, int window_bits)
        {
            try{
                namespace io = boost::iostreams;
                std::stringstream ss;
                std::stringstream compressed(source);
                io::filtering_streambuf<io::input> pipe;
                io::zlib_params params;
                params.window_bits = window_bits;
                pipe.push(io::zlib_decompressor(params));
                pipe.push(compressed);
                copy(pipe, ss);
                destination = ss.str();
                return true;
            }
            catch(...)
            {
                return false;
            }
        }

        bool gunzip(const std::string& source, std::string& destination)
        {
            try{
                namespace io = boost::iostreams;
                std::stringstream ss;
                std::stringstream compressed(source);
                io::filtering_streambuf<io::input> pipe;
                pipe.push(io::gzip_decompressor());
                pipe.push(compressed);
                copy(pipe, ss);
                destination = ss.str();
                return true;
            }
            catch(...)
            {
                return false;
            }
        }

        bool inflate(const std::string& source, std::string& destination)
        {
            bool ret_val = unzip(source, destination, -15); //
            if(!ret_val)                                    // The magic numbers -15 & 47 were taken from the agent8 webkit
                ret_val = unzip(source, destination, 47);   //
            return ret_val;
        }

        std::string zip(const std::string& source)
        {
            namespace io = boost::iostreams;
            std::stringstream destination;
            std::stringstream original(source);
            io::filtering_streambuf<io::input> pipe;
            pipe.push(io::zlib_compressor());
            pipe.push(original);
            io::copy(pipe, destination);
            return destination.str();

        }
