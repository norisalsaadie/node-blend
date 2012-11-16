#include "blend.hpp"
#include "palette.hpp"
#include "image_data.hpp"
#include "png_io.hpp"
#include "jpeg_io.hpp"

#include <sstream>
#include <cmath> // for fmod

//#define USE_BOOST_GIL

#ifdef USE_BOOST_GIL
#include <boost/gil/gil_all.hpp>
#include <boost/gil/extension/toolbox/hsl.hpp>
#endif

using namespace v8;
using namespace node;

unsigned int hexToUInt32Color(char *hex) {
    if (!hex) return 0;
    if (hex[0] == '#') hex++;
    int len = strlen(hex);
    if (len != 6 && len != 8) return 0;

    unsigned int color = 0;
    std::stringstream ss;
    ss << std::hex << hex;
    ss >> color;

    if (len == 8) {
        // Circular shift to get from RGBA to ARGB.
        return (color << 24) | ((color & 0xFF00) << 8) | ((color & 0xFF0000) >> 8) | ((color & 0xFF000000) >> 24);
    } else {
        return 0xFF000000 | ((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16);
    }
}

#ifdef USE_BOOST_GIL
void rgb2hsl(unsigned r, unsigned g, unsigned b,
             double & h, double & s, double & l) {
    using namespace boost;
    using namespace gil;
    using namespace hsl_color_space;
    rgb8_pixel_t rgb_src(r,g,b);
    hsl32f_pixel_t hsl_src;
    color_convert(rgb_src, hsl_src);
    h = get_color(hsl_src,hue_t());
    s = get_color(hsl_src,saturation_t());
    l = get_color(hsl_src,lightness_t());
}

void hsl2rgb(double h, double s, double l,
             unsigned & r, unsigned & g, unsigned & b) {
    using namespace boost;
    using namespace gil;
    using namespace hsl_color_space;
    hsl32f_pixel_t hsl_src(h,s,l);
    rgb8_pixel_t rgb_dst;
    color_convert(hsl_src, rgb_dst);
    r = get_color(rgb_dst,red_t());
    g = get_color(rgb_dst,green_t());
    b = get_color(rgb_dst,blue_t());
}

#else

void rgb2hsl(unsigned red, unsigned green, unsigned blue,
             double & h, double & s, double & l) {
    double r = red/255.0;
    double g = green/255.0;
    double b = blue/255.0;
    double max = std::max(r,std::max(g,b));
    double min = std::min(r,std::min(g,b));
    double delta = max - min;
    double gamma = max + min;
    h = 0.0, s = 0.0, l = gamma / 2.0;
    if (delta) {
        s = l > 0.5 ? delta / (2.0 - gamma) : delta / gamma;
        if (max == r && max != g) h = (g - b) / delta + (g < b ? 6.0 : 0.0);
        if (max == g && max != b) h = (b - r) / delta + 2.0;
        if (max == b && max != r) h = (r - g) / delta + 4.0;
        h /= 6.0;
    }
}

double hueToRGB(double m1, double m2, double h) {
    h = fmod(h+1,1);
    if (h * 6 < 1) return m1 + (m2 - m1) * h * 6;
    if (h * 2 < 1) return m2;
    if (h * 3 < 2) return m1 + (m2 - m1) * (0.66666 - h) * 6;
    return m1;
};

void hsl2rgb(double h, double s, double l,
             unsigned & r, unsigned & g, unsigned & b) {
    if (!s) {
        r = g = b = static_cast<unsigned>(l * 255);
    }


    double m2 = (l <= 0.5) ? l * (s + 1) : l + s - l * s;
    double m1 = l * 2 - m2;
    r = static_cast<unsigned>(hueToRGB(m1, m2, h + 0.33333) * 255);
    g = static_cast<unsigned>(hueToRGB(m1, m2, h) * 255);
    b = static_cast<unsigned>(hueToRGB(m1, m2, h - 0.33333) * 255);
}

#endif

Handle<Value> rgb2hsl2(const Arguments& args) {
    HandleScope scope;
    if (args.Length() != 3) {
        return TYPE_EXCEPTION("Please pass r,g,b integer values as three arguments");
    }
    if (!args[0]->IsNumber() || !args[1]->IsNumber() || !args[2]->IsNumber()) {
        return TYPE_EXCEPTION("Please pass r,g,b integer values as three arguments");
    }
    unsigned r,g,b;
    r = args[0]->IntegerValue();
    g = args[1]->IntegerValue();
    b = args[2]->IntegerValue();
    Local<Array> hsl = Array::New(3);
    double h,s,l;
    rgb2hsl(r,g,b,h,s,l);
    hsl->Set(0,Number::New(h));
    hsl->Set(1,Number::New(s));
    hsl->Set(2,Number::New(l));
    return scope.Close(hsl);
}

Handle<Value> hsl2rgb2(const Arguments& args) {
    HandleScope scope;
    if (args.Length() != 3) {
        return TYPE_EXCEPTION("Please pass hsl fractional values as three arguments");
    }
    if (!args[0]->IsNumber() || !args[1]->IsNumber() || !args[2]->IsNumber()) {
        return TYPE_EXCEPTION("Please pass hsl fractional values as three arguments");
    }
    double h,s,l;
    h = args[0]->NumberValue();
    s = args[1]->NumberValue();
    l = args[2]->NumberValue();
    Local<Array> rgb = Array::New(3);
    unsigned r,g,b;
    hsl2rgb(h,s,l,r,g,b);
    rgb->Set(0,Integer::New(r));
    rgb->Set(1,Integer::New(g));
    rgb->Set(2,Integer::New(b));
    return scope.Close(rgb);
}


Handle<Value> Blend(const Arguments& args) {
    HandleScope scope;
    std::auto_ptr<BlendBaton> baton(new BlendBaton());

    Local<Object> options;
    if (args.Length() == 0 || !args[0]->IsArray()) {
        return TYPE_EXCEPTION("First argument must be an array of Buffers.");
    } else if (args.Length() == 1) {
        return TYPE_EXCEPTION("Second argument must be a function");
    } else if (args.Length() == 2) {
        // No options provided.
        if (!args[1]->IsFunction()) {
            return TYPE_EXCEPTION("Second argument must be a function.");
        }
        baton->callback = Persistent<Function>::New(Local<Function>::Cast(args[1]));
    } else if (args.Length() >= 3) {
        if (!args[1]->IsObject()) {
            return TYPE_EXCEPTION("Second argument must be a an options object.");
        }
        options = Local<Object>::Cast(args[1]);

        if (!args[2]->IsFunction()) {
            return TYPE_EXCEPTION("Third argument must be a function.");
        }
        baton->callback = Persistent<Function>::New(Local<Function>::Cast(args[2]));
    }

    // Validate options
    if (!options.IsEmpty()) {
        baton->quality = options->Get(String::NewSymbol("quality"))->Int32Value();

        Local<Value> format_val = options->Get(String::NewSymbol("format"));
        if (!format_val.IsEmpty() && format_val->IsString()) {
            if (strcmp(*String::AsciiValue(format_val), "jpeg") == 0 ||
                    strcmp(*String::AsciiValue(format_val), "jpg") == 0) {
                baton->format = BLEND_FORMAT_JPEG;
                if (baton->quality == 0) baton->quality = 80;
                else if (baton->quality < 0 || baton->quality > 100) {
                    return TYPE_EXCEPTION("JPEG quality is range 0-100.");
                }
            } else if (strcmp(*String::AsciiValue(format_val), "png") == 0) {
                if (baton->quality == 1 || baton->quality > 256) {
                    return TYPE_EXCEPTION("PNG images must be quantized between 2 and 256 colors.");
                }
            } else {
                return TYPE_EXCEPTION("Invalid output format.");
            }
        }

        baton->reencode = options->Get(String::NewSymbol("reencode"))->BooleanValue();
        baton->width = options->Get(String::NewSymbol("width"))->Int32Value();
        baton->height = options->Get(String::NewSymbol("height"))->Int32Value();

        Local<Value> matte_val = options->Get(String::NewSymbol("matte"));
        if (!matte_val.IsEmpty() && matte_val->IsString()) {
            baton->matte = hexToUInt32Color(*String::AsciiValue(matte_val->ToString()));

            // Make sure we're reencoding in the case of single alpha PNGs
            if (baton->matte && !baton->reencode) {
                baton->reencode = true;
            }
        }

        Local<Value> palette_val = options->Get(String::NewSymbol("palette"));
        if (!palette_val.IsEmpty() && palette_val->IsObject()) {
            baton->palette = ObjectWrap::Unwrap<Palette>(palette_val->ToObject())->palette();
        }

        Local<Value> mode_val = options->Get(String::NewSymbol("mode"));
        if (!mode_val.IsEmpty() && mode_val->IsString()) {
            if (strcmp(*String::AsciiValue(mode_val), "octree") == 0 ||
                strcmp(*String::AsciiValue(mode_val), "o") == 0) {
                baton->mode = BLEND_MODE_OCTREE;
            }
            else if (strcmp(*String::AsciiValue(mode_val), "hextree") == 0 ||
                strcmp(*String::AsciiValue(mode_val), "h") == 0) {
                baton->mode = BLEND_MODE_HEXTREE;
            }
        }

        Local<Value> encoder_val = options->Get(String::NewSymbol("encoder"));
        if (!encoder_val.IsEmpty() && encoder_val->IsString()) {
            if (strcmp(*String::AsciiValue(encoder_val), "miniz") == 0) {
                baton->encoder = BLEND_ENCODER_MINIZ;
            }
            // default is libpng
        }

        int max_compression = Z_BEST_COMPRESSION;
        if (baton->encoder == BLEND_ENCODER_MINIZ) max_compression = MZ_UBER_COMPRESSION;
        baton->compression = options->Get(String::NewSymbol("compression"))->Int32Value();
        if (baton->compression <= 0) baton->compression = Z_DEFAULT_COMPRESSION;
        if (baton->compression > max_compression) {
            std::ostringstream msg;
            msg << "Compression level must be between 1 and "
                << max_compression;
            return TYPE_EXCEPTION(msg.str().c_str());
        }

        Local<Value> tint_val = options->Get(String::NewSymbol("tint"));
        if (!tint_val.IsEmpty() && tint_val->IsObject()) {
            Local<Object> tint = tint_val->ToObject();
            if (!tint.IsEmpty()) {
                baton->tint.identity = false;
                Local<Value> hue = tint->Get(String::NewSymbol("h"));
                if (!hue.IsEmpty() && hue->IsArray()) {
                    Local<Array> val_array = Local<Array>::Cast(hue);
                    if (val_array->Length() != 2) {
                        return TYPE_EXCEPTION("h array must be a pair of values");
                    }
                    baton->tint.h0 = val_array->Get(0)->NumberValue();
                    baton->tint.h1 = val_array->Get(1)->NumberValue();
                }
                Local<Value> sat = tint->Get(String::NewSymbol("s"));
                if (!sat.IsEmpty() && sat->IsArray()) {
                    Local<Array> val_array = Local<Array>::Cast(sat);
                    if (val_array->Length() != 2) {
                        return TYPE_EXCEPTION("s array must be a pair of values");
                    }
                    baton->tint.s0 = val_array->Get(0)->NumberValue();
                    baton->tint.s1 = val_array->Get(1)->NumberValue();
                }
                Local<Value> light = tint->Get(String::NewSymbol("l"));
                if (!light.IsEmpty() && light->IsArray()) {
                    Local<Array> val_array = Local<Array>::Cast(light);
                    if (val_array->Length() != 2) {
                        return TYPE_EXCEPTION("l array must be a pair of values");
                    }
                    baton->tint.l0 = val_array->Get(0)->NumberValue();
                    baton->tint.l1 = val_array->Get(1)->NumberValue();
                }
                Local<Value> alpha = tint->Get(String::NewSymbol("a"));
                if (!alpha.IsEmpty() && alpha->IsArray()) {
                    Local<Array> val_array = Local<Array>::Cast(alpha);
                    if (val_array->Length() != 2) {
                        return TYPE_EXCEPTION("a array must be a pair of values");
                    }
                    baton->tint.a0 = val_array->Get(0)->NumberValue();
                    baton->tint.a1 = val_array->Get(1)->NumberValue();
                }
            }
        }
    }

    Local<Array> images = Local<Array>::Cast(args[0]);
    uint32_t length = images->Length();
    if (length < 1 && !baton->reencode) {
        return TYPE_EXCEPTION("First argument must contain at least one Buffer.");
    } else if (length == 1 && !baton->reencode) {
        Local<Value> buffer = images->Get(0);
        if (Buffer::HasInstance(buffer)) {
            // Directly pass through buffer if it's the only one.
            Local<Value> argv[] = {
                Local<Value>::New(Null()),
                buffer
            };
            TRY_CATCH_CALL(Context::GetCurrent()->Global(), baton->callback, 2, argv);
            return scope.Close(Undefined());
        } else {
            // Check whether the argument is a complex image with offsets etc.
            // In that case, we don't throw but continue going through the blend
            // process below.
            bool valid = false;
            if (buffer->IsObject()) {
                Local<Object> props = buffer->ToObject();
                valid = props->Has(String::NewSymbol("buffer")) &&
                        Buffer::HasInstance(props->Get(String::NewSymbol("buffer")));
            }
            if (!valid) {
                return TYPE_EXCEPTION("All elements must be Buffers or objects with a 'buffer' property.");
            }
        }
    }

    if (!(length >= 1 || (baton->width > 0 && baton->height > 0))) {
        return TYPE_EXCEPTION("Without buffers, you have to specify width and height.");
    }

    if (baton->width < 0 || baton->height < 0) {
        return TYPE_EXCEPTION("Image dimensions must be greater than 0.");
    }

    for (uint32_t i = 0; i < length; i++) {
        ImagePtr image(new Image());
        Local<Value> buffer = images->Get(i);
        if (Buffer::HasInstance(buffer)) {
            image->buffer = Persistent<Object>::New(buffer->ToObject());
        } else if (buffer->IsObject()) {
            Local<Object> props = buffer->ToObject();
            if (props->Has(String::NewSymbol("buffer"))) {
                Local<Value> buffer = props->Get(String::NewSymbol("buffer"));
                if (Buffer::HasInstance(buffer)) {
                    image->buffer = Persistent<Object>::New(buffer->ToObject());
                }
            }
            image->x = props->Get(String::NewSymbol("x"))->Int32Value();
            image->y = props->Get(String::NewSymbol("y"))->Int32Value();
        }

        if (image->buffer.IsEmpty()) {
            return TYPE_EXCEPTION("All elements must be Buffers or objects with a 'buffer' property.");
        }

        image->data = (unsigned char*)node::Buffer::Data(image->buffer);
        image->dataLength = node::Buffer::Length(image->buffer);
        baton->images.push_back(image);
    }

    QUEUE_WORK(baton.release(), Work_Blend, Work_AfterBlend);

    return scope.Close(Undefined());
}


inline void Blend_CompositePixel(unsigned int& target, unsigned int& source) {
    if (source <= 0x00FFFFFF) {
        // Top pixel is fully transparent.
        // <do nothing>
    } else if (source >= 0xFF000000 || target <= 0x00FFFFFF) {
        // Top pixel is fully opaque or bottom pixel is fully transparent.
        target = source;
    } else {
        // Both pixels have transparency.
        // From http://trac.mapnik.org/browser/trunk/include/mapnik/graphics.hpp#L337
        unsigned int a1 = (source >> 24) & 0xff;
        unsigned int r1 = source & 0xff;
        unsigned int g1 = (source >> 8) & 0xff;
        unsigned int b1 = (source >> 16) & 0xff;

        unsigned int a0 = (target >> 24) & 0xff;
        unsigned int r0 = (target & 0xff) * a0;
        unsigned int g0 = ((target >> 8) & 0xff) * a0;
        unsigned int b0 = ((target >> 16) & 0xff) * a0;

        a0 = ((a1 + a0) << 8) - a0 * a1;
        r0 = ((((r1 << 8) - r0) * a1 + (r0 << 8)) / a0);
        g0 = ((((g1 << 8) - g0) * a1 + (g0 << 8)) / a0);
        b0 = ((((b1 << 8) - b0) * a1 + (b0 << 8)) / a0);
        a0 = a0 >> 8;
        target = (a0 << 24) | (b0 << 16) | (g0 << 8) | (r0);
    }
}


void Blend_Composite(unsigned int *target, BlendBaton *baton, Image *image) {
    unsigned int *source = image->reader->surface;

    int sourceX = std::max(0, -image->x);
    int sourceY = std::max(0, -image->y);
    int sourcePos = sourceY * image->width + sourceX;

    int width = image->width - sourceX - std::max(0, image->x + image->width - baton->width);
    int height = image->height - sourceY - std::max(0, image->y + image->height - baton->height);

    int targetX = std::max(0, image->x);
    int targetY = std::max(0, image->y);
    int targetPos = targetY * baton->width + targetX;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Blend_CompositePixel(target[targetPos + x], source[sourcePos + x]);
        }

        sourcePos += image->width;
        targetPos += baton->width;
    }
}

void Blend_Encode(image_data_32 const& image, BlendBaton* baton, bool alpha) {
    try {
        if (baton->format == BLEND_FORMAT_JPEG) {
            if (baton->quality == 0) baton->quality = 80;
            save_as_jpeg(baton->stream, baton->quality, image);
        } else {
            // Save as PNG.
            int strategy = Z_DEFAULT_STRATEGY;
            int trans_mode = -1;
            double gamma = -1;
            bool use_miniz = false;
            if (baton->encoder == BLEND_ENCODER_MINIZ) use_miniz = true;
            if (baton->palette.get() && baton->palette->valid()) {
                save_as_png8_pal(baton->stream, image, *baton->palette, baton->compression, strategy, use_miniz);
            } else if (baton->quality > 0) {
                // Paletted PNG.
                if (alpha && baton->mode == BLEND_MODE_HEXTREE) {
                    save_as_png8_hex(baton->stream, image, baton->quality, baton->compression, strategy, trans_mode, gamma, use_miniz);
                } else {
                    save_as_png8_oct(baton->stream, image, baton->quality, baton->compression, strategy, trans_mode, use_miniz);
                }
            } else {
                save_as_png(baton->stream, image, baton->compression, strategy, alpha, use_miniz);
            }
        }
    } catch (const std::exception& ex) {
        baton->message = ex.what();
    }
}

WORKER_BEGIN(Work_Blend) {
    BlendBaton* baton = static_cast<BlendBaton*>(req->data);

    int total = baton->images.size();
    bool alpha = true;
    int size = 0;

    // Iterate from the last to first image because we potentially don't have
    // to decode all images if there's an opaque one.
    Images::reverse_iterator rit = baton->images.rbegin();
    Images::reverse_iterator rend = baton->images.rend();
    for (int index = total - 1; rit != rend; rit++, index--) {
        // If an image that is higher than the current is opaque, stop alltogether.
        if (!alpha) break;

        Image *image = &**rit;
        std::auto_ptr<ImageReader> layer(ImageReader::create(image->data, image->dataLength));

        // Error out on invalid images.
        if (layer.get() == NULL || layer->width == 0 || layer->height == 0) {
            baton->message = layer->message;
            WORKER_END();
        }

        int visibleWidth = (int)layer->width + image->x;
        int visibleHeight = (int)layer->height + image->y;

        // The first image that is in the viewport sets the width/height, if not user supplied.
        if (baton->width <= 0) baton->width = std::max(0, visibleWidth);
        if (baton->height <= 0) baton->height = std::max(0, visibleHeight);

        // Skip images that are outside of the viewport.
        if (visibleWidth <= 0 || visibleHeight <= 0 || image->x >= baton->width || image->y >= baton->height) {
            // Remove this layer from the list of layers we consider blending.
            continue;
        }

        // Short-circuit when we're not reencoding.
        if (size == 0 && !layer->alpha && !baton->reencode &&
            image->x == 0 && image->y == 0 &&
            (int)layer->width == baton->width && (int)layer->height == baton->height)
        {
            baton->stream.write((char *)image->data, image->dataLength);
            WORKER_END();
        }

        if (!layer->decode()) {
            // Decoding failed.
            baton->message = layer->message;
            WORKER_END();
        }
        else if (layer->warnings.size()) {
            std::vector<std::string>::iterator pos = layer->warnings.begin();
            std::vector<std::string>::iterator end = layer->warnings.end();
            for (; pos != end; pos++) {
                std::ostringstream msg;
                msg << "Layer " << index << ": " << *pos;
                baton->warnings.push_back(msg.str());
            }
        }

        bool coversWidth = image->x <= 0 && visibleWidth >= baton->width;
        bool coversHeight = image->y <= 0 && visibleHeight >= baton->height;
        if (!layer->alpha && coversWidth && coversHeight) {
            // Skip decoding more layers.
            alpha = false;
        }

        // Convenience aliases.
        image->width = layer->width;
        image->height = layer->height;
        image->reader = layer;
        size++;
    }

    // Now blend images.
    int pixels = baton->width * baton->height;
    if (pixels <= 0) {
        std::ostringstream msg;
        msg << "Image dimensions " << baton->width << "x" << baton->height << " are invalid";
        baton->message = msg.str();
        WORKER_END();
    }

    unsigned int *target = (unsigned int *)malloc(sizeof(unsigned int) * pixels);
    if (!target) {
        baton->message = "Memory allocation failed";
        WORKER_END();
    }

    // When we don't actually have transparent pixels, we don't need to set
    // the matte.
    if (alpha) {
        // We can't use memset here because it converts the color to a 1-byte value.
        for (int i = 0; i < pixels; i++) {
            target[i] = baton->matte;
        }
    }

    for (Images::iterator it = baton->images.begin(); it != baton->images.end(); it++) {
        Image *image = &**it;
        if (image->reader.get()) {
            Blend_Composite(target, baton, image);
        }
    }

    image_data_32 image(baton->width, baton->height, (unsigned int*)target);
    if (!baton->tint.identity) {
        for (unsigned int y = 0; y < image.height(); ++y)
        {
            unsigned int* row_from = image.getRow(y);
            for (unsigned int x = 0; x < image.width(); ++x)
            {
                unsigned rgba = row_from[x];
                double h,s,l;
                unsigned r = rgba & 0xff;
                unsigned g = (rgba >> 8 ) & 0xff;
                unsigned b = (rgba >> 16) & 0xff;
                unsigned a = (rgba >> 24) & 0xff;
                rgb2hsl(r,g,b,h,s,l);
                hsl2rgb(h,s,l,r,g,b);
                row_from[x] = (a << 24) | (b << 16) | (g << 8) | (r);
            }
        }
    }

    Blend_Encode(image, baton, alpha);
    free(target);
    target = NULL;
    WORKER_END();
}

WORKER_BEGIN(Work_AfterBlend) {
    HandleScope scope;
    BlendBaton* baton = static_cast<BlendBaton*>(req->data);

    if (!baton->message.length()) {
        Local<Array> warnings = Array::New();
        std::vector<std::string>::iterator pos = baton->warnings.begin();
        std::vector<std::string>::iterator end = baton->warnings.end();
        for (int i = 0; pos != end; pos++, i++) {
            warnings->Set(i, String::New((*pos).c_str()));
        }

        std::string result = baton->stream.str();
        Local<Value> argv[] = {
            Local<Value>::New(Null()),
            Local<Value>::New(Buffer::New((char *)result.data(), result.length())->handle_),
            Local<Value>::New(warnings)
        };
        TRY_CATCH_CALL(Context::GetCurrent()->Global(), baton->callback, 3, argv);
    } else {
        Local<Value> argv[] = {
            Local<Value>::New(Exception::Error(String::New(baton->message.c_str())))
        };

        assert(!baton->callback.IsEmpty());
        TRY_CATCH_CALL(Context::GetCurrent()->Global(), baton->callback, 1, argv);
    }

    delete baton;
    WORKER_END();
}

extern "C" void init(Handle<Object> target) {
    NODE_SET_METHOD(target, "blend", Blend);
    NODE_SET_METHOD(target, "rgb2hsl2", rgb2hsl2);
    NODE_SET_METHOD(target, "hsl2rgb2", hsl2rgb2);
    Palette::Initialize(target);

    target->Set(
        String::NewSymbol("libpng"),
        String::NewSymbol(PNG_LIBPNG_VER_STRING),
        static_cast<PropertyAttribute>(ReadOnly | DontDelete)
    );

    target->Set(
        String::NewSymbol("libjpeg"),
        Integer::New(JPEG_LIB_VERSION),
        static_cast<PropertyAttribute>(ReadOnly | DontDelete)
    );
}
