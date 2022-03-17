#include "GLSelectionRectangle.hpp"
#include "Camera.hpp"
#include "CameraUtils.hpp"
#include "3DScene.hpp"
#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include <igl/project.h>

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

    void GLSelectionRectangle::start_dragging(const Vec2d& mouse_position, EState state)
    {
        if (is_dragging() || (state == Off))
            return;

        m_state = state;
        m_start_corner = mouse_position;
        m_end_corner = mouse_position;
    }

    void GLSelectionRectangle::dragging(const Vec2d& mouse_position)
    {
        if (!is_dragging())
            return;

        m_end_corner = mouse_position;
    }

    std::vector<unsigned int> GLSelectionRectangle::stop_dragging(const GLCanvas3D& canvas, const std::vector<Vec3d>& points)
    {
        std::vector<unsigned int> out;

        if (!is_dragging())
            return out;

        m_state = Off;

        // bounding box created from the rectangle corners - will take care of order of the corners
        BoundingBox rectangle(Points{ Point(m_start_corner.cast<coord_t>()), Point(m_end_corner.cast<coord_t>()) });

        // Iterate over all points and determine whether they're in the rectangle.
        const Camera &camera = wxGetApp().plater()->get_camera();
        Points points_2d = CameraUtils::project(camera, points);
        for (int i = 0; i<points.size(); ++i)
            if (rectangle.contains(points_2d[i]))
                out.push_back(i);

        return out;
    }

    void GLSelectionRectangle::stop_dragging()
    {
        if (is_dragging())
            m_state = Off;
    }

    void GLSelectionRectangle::render(const GLCanvas3D& canvas)
    {
        if (!is_dragging())
            return;

        const Size cnv_size = canvas.get_canvas_size();
#if ENABLE_GL_SHADERS_ATTRIBUTES
        const float cnv_width = (float)cnv_size.get_width();
        const float cnv_height = (float)cnv_size.get_height();
        if (cnv_width == 0.0f || cnv_height == 0.0f)
            return;

        const float cnv_inv_width = 1.0f / cnv_width;
        const float cnv_inv_height = 1.0f / cnv_height;
        const float left = 2.0f * (get_left() * cnv_inv_width - 0.5f);
        const float right = 2.0f * (get_right() * cnv_inv_width - 0.5f);
        const float top = -2.0f * (get_top() * cnv_inv_height - 0.5f);
        const float bottom = -2.0f * (get_bottom() * cnv_inv_height - 0.5f);
#else
        const Camera& camera = wxGetApp().plater()->get_camera();
        const float inv_zoom = (float)camera.get_inv_zoom();

        const float cnv_half_width = 0.5f * (float)cnv_size.get_width();
        const float cnv_half_height = 0.5f * (float)cnv_size.get_height();
        if (cnv_half_width == 0.0f || cnv_half_height == 0.0f)
            return;

        const Vec2d start(m_start_corner.x() - cnv_half_width, cnv_half_height - m_start_corner.y());
        const Vec2d end(m_end_corner.x() - cnv_half_width, cnv_half_height - m_end_corner.y());

        const float left   = (float)std::min(start.x(), end.x()) * inv_zoom;
        const float top    = (float)std::max(start.y(), end.y()) * inv_zoom;
        const float right  = (float)std::max(start.x(), end.x()) * inv_zoom;
        const float bottom = (float)std::min(start.y(), end.y()) * inv_zoom;
#endif // ENABLE_GL_SHADERS_ATTRIBUTES

        glsafe(::glLineWidth(1.5f));
#if !ENABLE_LEGACY_OPENGL_REMOVAL
        float color[3];
        color[0] = (m_state == Select) ? 0.3f : 1.0f;
        color[1] = (m_state == Select) ? 1.0f : 0.3f;
        color[2] = 0.3f;
        glsafe(::glColor3fv(color));
#endif // !ENABLE_LEGACY_OPENGL_REMOVAL

        glsafe(::glDisable(GL_DEPTH_TEST));

#if !ENABLE_GL_SHADERS_ATTRIBUTES
        glsafe(::glPushMatrix());
        glsafe(::glLoadIdentity());
        // ensure that the rectangle is renderered inside the frustrum
        glsafe(::glTranslated(0.0, 0.0, -(camera.get_near_z() + 0.5)));
        // ensure that the overlay fits the frustrum near z plane
        const double gui_scale = camera.get_gui_scale();
        glsafe(::glScaled(gui_scale, gui_scale, 1.0));
#endif // !ENABLE_GL_SHADERS_ATTRIBUTES

        glsafe(::glPushAttrib(GL_ENABLE_BIT));
        glsafe(::glLineStipple(4, 0xAAAA));
        glsafe(::glEnable(GL_LINE_STIPPLE));

#if ENABLE_LEGACY_OPENGL_REMOVAL
#if ENABLE_GL_SHADERS_ATTRIBUTES
        GLShaderProgram* shader = wxGetApp().get_shader("flat_attr");
#else
        GLShaderProgram* shader = wxGetApp().get_shader("flat");
#endif // ENABLE_GL_SHADERS_ATTRIBUTES
        if (shader != nullptr) {
            shader->start_using();

            if (!m_rectangle.is_initialized() || !m_old_start_corner.isApprox(m_start_corner) || !m_old_end_corner.isApprox(m_end_corner)) {
                m_old_start_corner = m_start_corner;
                m_old_end_corner = m_end_corner;
                m_rectangle.reset();

                GLModel::Geometry init_data;
                init_data.format = { GLModel::Geometry::EPrimitiveType::LineLoop, GLModel::Geometry::EVertexLayout::P2 };
                init_data.reserve_vertices(4);
                init_data.reserve_indices(4);

                // vertices
                init_data.add_vertex(Vec2f(left, bottom));
                init_data.add_vertex(Vec2f(right, bottom));
                init_data.add_vertex(Vec2f(right, top));
                init_data.add_vertex(Vec2f(left, top));

                // indices
                init_data.add_index(0);
                init_data.add_index(1);
                init_data.add_index(2);
                init_data.add_index(3);

                m_rectangle.init_from(std::move(init_data));
            }

#if ENABLE_GL_SHADERS_ATTRIBUTES
            shader->set_uniform("view_model_matrix", Transform3d::Identity());
            shader->set_uniform("projection_matrix", Transform3d::Identity());
#endif // ENABLE_GL_SHADERS_ATTRIBUTES

            m_rectangle.set_color(ColorRGBA((m_state == Select) ? 0.3f : 1.0f, (m_state == Select) ? 1.0f : 0.3f, 0.3f, 1.0f));
            m_rectangle.render();
            shader->stop_using();
        }
#else
        ::glBegin(GL_LINE_LOOP);
        ::glVertex2f((GLfloat)left, (GLfloat)bottom);
        ::glVertex2f((GLfloat)right, (GLfloat)bottom);
        ::glVertex2f((GLfloat)right, (GLfloat)top);
        ::glVertex2f((GLfloat)left, (GLfloat)top);
        glsafe(::glEnd());
#endif // ENABLE_LEGACY_OPENGL_REMOVAL

        glsafe(::glPopAttrib());

#if !ENABLE_GL_SHADERS_ATTRIBUTES
        glsafe(::glPopMatrix());
#endif // !ENABLE_GL_SHADERS_ATTRIBUTES
    }

} // namespace GUI
} // namespace Slic3r
