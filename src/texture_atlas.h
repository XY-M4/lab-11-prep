#ifndef TEXTURE_ATLAS_H
#define TEXTURE_ATLAS_H
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
#include <limits>
#include "vector_util.h"
#include <SFML/Graphics.hpp>
#include <stdexcept>

// RectPacker maintains two offsets into an overall bin size:
//  - A line y-offset, representing the current horizontal line on which to place new rectangles
//      horizontally along (the line corresponding to the rectangle tops)
//  - An x-offset into the line, representing the x-coordinate to align the next rectangle left-side
// Furthermore it maintains the maximum texture height encountered along the current line.
// The RectPacker places rectangles left to right along this line until it cannot fit the next
//  rectangle. Once this happens, the line is shifted down to meet the bottom of the tallest texture
//  placed on the line, and the x-offset is reset. This continues until the next rectangle
//  packed cannot be fit on the line within the bin space.
class RectPacker {
public:
    RectPacker(const sf::Vector2f& size) : mSize(size) {};

    // pack returns { succeeded, rect id }. If not succeeded, id is irrelevant (but ideally should
    // be set to the maximum possible value).
    std::pair<bool, size_t> pack(const sf::Vector2f& rectSize) { 
        const size_t invalidID = std::numeric_limits<size_t>::max();

        if (rectSize.x > mSize.x || rectSize.y > mSize.y) {
            return {false, invalidID};
        }

        float x = mX;
        float y = mY;
        float rowHeight = mRowHeight;

        // Start a new row if the rectangle does not fit horizontally
        if (x + rectSize.x > mSize.x) {
            x = 0.f;
            y += rowHeight;
            rowHeight = 0.f;
        }

        // Reject if the rectangle does not fit vertically
        if (y + rectSize.y > mSize.y) {
            return {false, invalidID};
        }

        // Store the rectangle and assign its ID
        size_t rectID = mRects.size();
        mRects.emplace_back(sf::Vector2f{x, y}, rectSize);

        // Update packing state
        mX = x + rectSize.x;
        mY = y;

        if (rectSize.y > rowHeight) {
            rowHeight = rectSize.y;
        }

        mRowHeight = rowHeight;

        return {true, rectID};
    }

        // Returns the coordinates and size of a previously returned rectangle id.
        // Note that in this coordinate system, y points downwards from the origin at the top-left.
        sf::FloatRect getBounds(size_t rectID) const { 
            assert(rectID < mRects.size());
            return mRects[rectID];
        }

    private:
        sf::Vector2f mSize;
        std::vector<sf::FloatRect> mRects;
        float mX = 0.f;
        float mY = 0.f;
        float mRowHeight = 0.f;
    };

// Returns a new image which pages the given img with *pixels* pixels on each side.
// The colour of new border pixels are taken by sampling the closest valid pixel in the original
// image.
//
// NOTE: a production implementation might also support other edge wrapping behaviors, like cycling
// around to the other side of the texture. See https://learnopengl.com/Getting-started/Textures
// Our implementation corresponds to GL_CLAMP_TO_EDGE
inline sf::Image padImage(const sf::Image& img, uint pixels) { 
    sf::Vector2u size = img.getSize();

    sf::Vector2u paddedSize{
        size.x + 2 * pixels,
        size.y + 2 * pixels
    };

    sf::Image padded(paddedSize);

    for (unsigned int y = 0; y < paddedSize.y; ++y) {
        for (unsigned int x = 0; x < paddedSize.x; ++x) {

            // Find the nearest valid pixel in the original image
            unsigned int srcX = std::clamp(
                static_cast<int>(x) - static_cast<int>(pixels),
                0,
                static_cast<int>(size.x) - 1
            );

            unsigned int srcY = std::clamp(
                static_cast<int>(y) - static_cast<int>(pixels),
                0,
                static_cast<int>(size.y) - 1
            );

            padded.setPixel(
                {x, y},
                img.getPixel({srcX, srcY})
            );
        }
    }

    return padded;
}

using TextureID = size_t;

class TextureAtlas {
public:
    TextureAtlas(sf::Vector2u binSize, size_t padding)
        : mTexture(binSize), mPacker(sf::Vector2f(mTexture.getSize())), mPadding{padding} {}

    TextureID registerTexture(const std::string& filepath) { 
        // Load the original image
        sf::Image image;

        if (!image.loadFromFile(filepath)) {
            throw std::runtime_error("Failed to load image: " + filepath);
        }

        // Add padding around the image
        sf::Image padded = padImage(image, static_cast<uint>(mPadding));

        // Pack the padded rectangle into the atlas
        auto [success, id] = mPacker.pack(
            sf::Vector2f(padded.getSize())
        );

        if (!success) {
            throw std::runtime_error("Texture atlas is full");
        }

        // Get the packed rectangle position
        sf::FloatRect bounds = mPacker.getBounds(id);

        sf::Vector2u position{
            static_cast<unsigned int>(bounds.position.x),
            static_cast<unsigned int>(bounds.position.y)
        };

        // Write the padded image into the atlas texture
        mTexture.update(padded, position);

        return id;
    }

        sf::FloatRect getBoundsUV(TextureID id) const { 
            sf::FloatRect bounds = mPacker.getBounds(id);
            sf::Vector2f atlasSize(mTexture.getSize());

            float padding = static_cast<float>(mPadding);

            // Exclude padding
            float width = bounds.size.x - 2.f * padding;
            float height = bounds.size.y - 2.f * padding;

            // Normalize X coordinate
            float u = (bounds.position.x + padding) / atlasSize.x;

            // Convert top-left origin to OpenGL bottom-left origin
            float v = (atlasSize.y - bounds.position.y
                    - bounds.size.y + padding) / atlasSize.y;

            // Normalize dimensions
            float uvWidth = width / atlasSize.x;
            float uvHeight = height / atlasSize.y;

            return sf::FloatRect(
                sf::Vector2f{u, v},
                sf::Vector2f{uvWidth, uvHeight}
            );
        }

    sf::Vector2u getSizePixels(TextureID id) const { 
        sf::FloatRect bounds = mPacker.getBounds(id);
        float padding = static_cast<float>(mPadding);

        return {
            static_cast<unsigned int>(bounds.size.x - 2.f * padding),
            static_cast<unsigned int>(bounds.size.y - 2.f * padding)
        };
    }

    const sf::Texture& getTexture() const { return mTexture; }

private:
    sf::Texture mTexture;
    RectPacker mPacker;
    size_t mPadding;
};

#endif  // TEXTURE_ATLAS_H
