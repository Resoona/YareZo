#ifndef YARE_ENTITY_H
#define YARE_ENTITY_H

#include "Graphics/Components/Mesh.h"
#include "Graphics/Components/Material.h"
#include "Graphics/Components/Transform.h"

#include <string>
#include <vector>

namespace Yare::Graphics {
    class Entity {
    public:
        Entity() {};
        Entity(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material);
        Entity(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material, const Transform& transform);
        ~Entity();

        void setMesh(std::shared_ptr<Mesh> mesh) { m_Mesh = mesh; }
        void setMaterial(std::shared_ptr<Material> material) { m_Material = material; }
        void setTransform(Transform& transform) { m_Transform = transform; }

        const std::shared_ptr<Mesh>       getMesh()      const { return m_Mesh; }
        const std::shared_ptr<Material>   getMaterial()  const { return m_Material; }
        const Transform&                  getTransform() const { return m_Transform; }

    private:
        std::shared_ptr<Mesh> m_Mesh;
        std::shared_ptr<Material> m_Material;
        Transform m_Transform;

        int m_ImageIdx = 0;
    };

}

#endif //YARE_ENTITY_H
