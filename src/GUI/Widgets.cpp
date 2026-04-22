#include "GUI/Widgets.hpp"

#pragma region Particles

void ParticleSystem::RemoveAll() {
    m_Particles.clear();
}

void ParticleSystem::Spawn(std::size_t count, sf::FloatRect area) {
    m_Particles.clear();

    const float centerX = area.position.x + area.size.x * 0.5f;

    for (int i = 0; i < count; ++i) {
        Particle& p = m_Particles.emplace_back();

        const float spawnSpread = area.size.x * 0.4f;

        p.Position = sf::Vector2f(
            centerX + (std::rand() / static_cast<float>(RAND_MAX) - 0.5f) * spawnSpread * 2.f,
            area.position.y + area.size.y - (std::rand() % 10)
        );

        const float angle = -60.f - (std::rand() / static_cast<float>(RAND_MAX)) * 60.f;
        const float angleRad = angle * 3.14159265f / 180.f;
        const float speed = 220.f + (std::rand() / static_cast<float>(RAND_MAX)) * 500.f;

        p.Velocity = sf::Vector2f(
            std::cos(angleRad) * speed,
            std::sin(angleRad) * speed * 2.f
        );

        p.Rotation = static_cast<float>(std::rand() % 360);
        p.RotationSpeed = -300.f + (std::rand() % 600);

        const float sizeRand = std::rand() / static_cast<float>(RAND_MAX);
        p.Size = 4.f + sizeRand * sizeRand * 12.f;

        p.Life = 0.7f + 0.5f * (std::rand() / static_cast<float>(RAND_MAX));
        p.ColorIndex = std::rand() % 5;

        const int shapeRoll = std::rand() % 10;

        if (shapeRoll < 4) p.Shape = Particle::Shapes::Circle; // 40% circles
        else if (shapeRoll < 7) p.Shape = Particle::Shapes::Square; // 30% squares
        else p.Shape = Particle::Shapes::Diamond; // 30% diamonds
    }
}

void ParticleSystem::Update(float deltaTime) {
    constexpr float Gravity = 400.f;
    constexpr float AirResistance = 0.98f;

    for (auto it = m_Particles.begin(); it != m_Particles.end(); ) {
        Particle& p = *it;

        p.Velocity.y += Gravity * deltaTime;
        p.Velocity *= AirResistance;
        p.Position += p.Velocity * deltaTime;
        p.Rotation += p.RotationSpeed * deltaTime;
        p.Life -= deltaTime * 0.32f;

        if (p.Life <= 0.f) it = m_Particles.erase(it);
        else ++it;
    }
}

void ParticleSystem::Render(
    sf::RenderTarget& target, const std::array<sf::Color, 5>& colors, float opacityFactor
) const {
    for (const Particle& p : m_Particles) {
        const float alpha = std::max(0.f, p.Life);
        sf::Color col = colors[p.ColorIndex];
        col.a = 255 * alpha * std::min(1.f, opacityFactor * 2.f);

        if (p.Shape == Particle::Shapes::Circle) {
            sf::CircleShape circle(p.Size * 0.5f);

            circle.setOrigin(sf::Vector2f(p.Size * 0.5f, p.Size * 0.5f));
            circle.setPosition(p.Position);
            circle.setFillColor(col);

            target.draw(circle);
        } else {
            sf::RectangleShape rect(sf::Vector2f(p.Size, p.Size));

            rect.setOrigin(sf::Vector2f(p.Size * 0.5f, p.Size * 0.5f));
            rect.setPosition(p.Position);
            rect.setRotation(sf::degrees(p.Shape == Particle::Shapes::Diamond ? p.Rotation + 45.f : p.Rotation));
            rect.setFillColor(col);

            target.draw(rect);
        }
    }
}

#pragma region Evaluation Bar

void EvaluationBar::SetEval(float eval) {
    m_LatestEvaluation = eval;
}

void EvaluationBar::Update(float deltaTime) {
    m_CurrentEvaluation = Utils::ExponentiallyMoveTo(
        m_CurrentEvaluation, m_LatestEvaluation, 7.5f * deltaTime
    );
}

void EvaluationBar::Render(
    sf::RenderTarget& target, sf::Vector2f position, sf::Vector2f size,
    bool flipped, const std::array<sf::Color, 2>& colors
) const {
    const float padding_y = size.y * 0.006f;
    const float padding_x = size.x * 0.11f;

    const float barHeight = size.y - 2.f * padding_y;
    const float barWidth = size.x - 2.f * padding_x;

    const float eval = flipped ? m_CurrentEvaluation : (1.f - m_CurrentEvaluation);
    const float midPos = barHeight * eval;

    sf::RenderTexture barRT(sf::Vector2u(barWidth, barHeight));
    barRT.clear(sf::Color::Transparent);

    Utils::RenderRoundedQuad(barRT, sf::Color::White, sf::Vector2f(), sf::Vector2f(barWidth, barHeight), 0.4f);

    sf::RectangleShape topFill(sf::Vector2f(barWidth, midPos));
    topFill.setFillColor(colors[!flipped]);
    barRT.draw(topFill, sf::BlendMultiply);

    sf::RectangleShape bottomFill(sf::Vector2f(barWidth, barHeight - midPos));
    bottomFill.setPosition(sf::Vector2f(0.f, midPos));
    bottomFill.setFillColor(colors[flipped]);
    barRT.draw(bottomFill, sf::BlendMultiply);

    barRT.display();

    sf::Sprite bar(barRT.getTexture());
    bar.setPosition(sf::Vector2f(padding_x, padding_y) + position);
    target.draw(bar);
}

#pragma region Button Panel

void ButtonPanel::RemoveAll() {
    m_Buttons.clear();
}

void ButtonPanel::AddButton(Button&& button) {
    m_Buttons.push_back(std::move(button));
}

bool ButtonPanel::Clicked() {
    for (Button& button : m_Buttons) {
        if (button.Hovered) {
            if (button.Callback) button.Callback(button);
            return true;
        }
    }

    return false;
}

void ButtonPanel::HandleMouseMoved(sf::Vector2i mousePosition, bool hovering) {
    Button* nowHovered = nullptr;

    if (hovering) {
        for (Button& button : m_Buttons) {
            if (
                mousePosition.x >= button.Position_X && mousePosition.x <= (button.Position_X + button.Width) &&
                mousePosition.y >= button.Position_Y && mousePosition.y <= (button.Position_Y + button.Height)
            ) {
                nowHovered = &button;
                break;
            }
        }
    }

    for (Button& button : m_Buttons) {
        const bool wasHovered = button.Hovered;
        button.Hovered = &button == nowHovered;

        if (button.Hovered && !wasHovered) {
            button.HoverEnterTime = std::chrono::steady_clock::now();
        }
    }

    m_Hovered = hovering;
}

void ButtonPanel::Update(float deltaTime) {
    m_Opacity = Utils::ExponentiallyMoveTo(
        m_Opacity, m_Hovered, 20.f * deltaTime
    );
}

void ButtonPanel::renderButton(
    sf::RenderTarget& target, const Button& button,
    sf::Color textColor, sf::Color overlayColor,
    const sf::Font& font, const sf::Texture& texture, float scale
) const {
    const float IconTextureWidth = static_cast<float>(texture.getSize().x) / static_cast<float>(m_Buttons.size() + 4);
    const float IconTextureHeight = static_cast<float>(texture.getSize().y);

    const float tx = static_cast<float>(button.TextureIndex) * IconTextureWidth;
    const float ty = 0.f;

    sf::Vertex vertices[] = {
        sf::Vertex(sf::Vector2f(button.Position_X, button.Position_Y), sf::Color::White, sf::Vector2f(tx, ty)),
        sf::Vertex(sf::Vector2f(button.Position_X + button.Width, button.Position_Y), sf::Color::White, sf::Vector2f(tx + IconTextureWidth, ty)),
        sf::Vertex(sf::Vector2f(button.Position_X, button.Position_Y + button.Height), sf::Color::White, sf::Vector2f(tx, ty + IconTextureHeight)),
        sf::Vertex(sf::Vector2f(button.Position_X + button.Width, button.Position_Y + button.Height), sf::Color::White, sf::Vector2f(tx + IconTextureWidth, ty + IconTextureHeight))
    };

    target.draw(vertices, 4, sf::PrimitiveType::TriangleStrip, &texture);

    if (button.Hovered) {
        Utils::RenderRoundedQuad(
            target, sf::Color(100, 100, 100, 100),
            sf::Vector2f(button.Position_X, button.Position_Y),
            sf::Vector2f(button.Width, button.Height),
            0.4f
        );
    }

    // button is focused
    if (
        button.Hovered &&
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - button.HoverEnterTime
        ).count() >= Button::TooltipThreshold
    ) {
        const std::string& note = button.Note[
            !button.Note[1].empty() && sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)
        ];

        if (!note.empty()) {
            const sf::Vector2u windowSize = target.getSize();

            const float paddingH = scale * 0.09f;
            const float paddingV = scale * 0.07f;
            const float gap = scale * 0.08f;
            const float arrowHalf = scale * 0.04f;
            const float arrowDepth = scale * 0.04f;
            const float maxBubbleW = scale * 1.1f;

            const unsigned int charSize = static_cast<unsigned int>(scale * 0.175f);
            const unsigned int fontSize = static_cast<unsigned int>(scale * 0.145f);

            sf::Text text(font, note, fontSize);
            text.setStyle(sf::Text::Style::Bold);

            std::string wrappedNote;
            {
                std::istringstream words(note);
                std::string word, line;

                while (words >> word) {
                    const std::string test = line.empty() ? word : (line + ' ' + word);
                    sf::Text probe(font, test, fontSize);
                    probe.setStyle(sf::Text::Style::Bold);
                    if (!line.empty() && word.size() > 1 && probe.getLocalBounds().size.x + paddingH * 2.f > maxBubbleW) {
                        wrappedNote += line + '\n';
                        line = word;
                    } else {
                        line = test;
                    }
                }

                wrappedNote += line;
            }

            text.setString(wrappedNote);

            const sf::FloatRect bounds = text.getLocalBounds();
            const float bubbleW = bounds.size.x + paddingH * 2.f;
            const float bubbleH = bounds.size.y + paddingV * 2.f;

            const float buttonCY = button.Position_Y + button.Height * 0.5f;

            float bubbleX = button.Position_X - gap - arrowDepth - bubbleW;
            float bubbleY = buttonCY - bubbleH * 0.5f;

            const float margin = 4.f;
            bubbleY = std::max(margin, std::min(bubbleY, static_cast<float>(windowSize.y) - bubbleH - margin));
            bubbleX = std::max(margin, bubbleX);

            const float arrowTipY = std::clamp(
                buttonCY,
                bubbleY + arrowHalf + 6.f,
                bubbleY + bubbleH - arrowHalf - 6.f
            );

            Utils::RenderRoundedQuad(target, overlayColor, sf::Vector2f(bubbleX, bubbleY), sf::Vector2f(bubbleW, bubbleH), 0.18f);

            const float arrowBaseX = bubbleX + bubbleW;
            const float arrowTipX = arrowBaseX + arrowDepth;

            const sf::Vertex arrow[] = {
                sf::Vertex(sf::Vector2f(arrowBaseX, arrowTipY - arrowHalf), overlayColor),
                sf::Vertex(sf::Vector2f(arrowBaseX, arrowTipY + arrowHalf), overlayColor),
                sf::Vertex(sf::Vector2f(arrowTipX, arrowTipY), overlayColor),
            };

            target.draw(arrow, 3, sf::PrimitiveType::Triangles);

            text.setFillColor(textColor);
            text.setOrigin(bounds.position);
            text.setPosition(sf::Vector2f(bubbleX + paddingH, bubbleY + paddingV));

            target.draw(text);
        }
    }
}

void ButtonPanel::Render(
    sf::RenderTarget& target,
    sf::Color backgroundColor, sf::Color textColor, sf::Color overlayColor,
    sf::Vector2f position, sf::Vector2f size,
    const sf::Font& font, const sf::Texture& texture, float scale
) const {
    for (const Button& button : m_Buttons) {
        renderButton(target, button, textColor, overlayColor, font, texture, scale);
    }

    // mask (for fade effect)
    Utils::RenderQuad(
        target, Utils::ConvertAlpha(backgroundColor, 1.f - m_Opacity), position, size
    );
}