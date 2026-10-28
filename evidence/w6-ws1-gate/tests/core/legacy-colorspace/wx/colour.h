#pragma once

// Y7: the part of wxColour (wxWidgets 3.3) that legacy colorspace.cpp uses:
// unsigned char channels, so wider or negative values wrap as they do in wx.

class wxColour {
public:
    wxColour() = default;
    wxColour(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha = 255)
        : m_red(red), m_green(green), m_blue(blue), m_alpha(alpha)
    {
    }
    unsigned char Red() const { return m_red; }
    unsigned char Green() const { return m_green; }
    unsigned char Blue() const { return m_blue; }
    unsigned char Alpha() const { return m_alpha; }

private:
    unsigned char m_red = 0, m_green = 0, m_blue = 0, m_alpha = 255;
};

inline const wxColour *const wxBLACK = new wxColour(0, 0, 0);
