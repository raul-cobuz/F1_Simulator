#include "Mesh.hpp"
namespace gps {

	namespace {
		GLuint getWhiteTexture() {
			static GLuint texture = 0;
			if (texture == 0) {
				unsigned char white[4] = {255, 255, 255, 255};
				glGenTextures(1, &texture);
				glBindTexture(GL_TEXTURE_2D, texture);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			}
			return texture;
		}
	}

	/* Mesh Constructor */
	Mesh::Mesh(std::vector<Vertex> vertices, std::vector<GLuint> indices, std::vector<Texture> textures) {

		this->vertices = vertices;
		this->indices = indices;
		this->textures = textures;

		this->setupMesh();
	}

	Buffers Mesh::getBuffers() {
	    return this->buffers;
	}

	/* Mesh drawing function - also applies associated textures */
	void Mesh::Draw(gps::Shader shader)	{

		shader.useShaderProgram();

		bool hasDiffuse = false;
		bool hasSpecular = false;

		//set textures
		for (GLuint i = 0; i < textures.size(); i++) {

			glActiveTexture(GL_TEXTURE0 + i);
			glUniform1i(glGetUniformLocation(shader.shaderProgram, this->textures[i].type.c_str()), i);
			glBindTexture(GL_TEXTURE_2D, this->textures[i].id);

			hasDiffuse = hasDiffuse || this->textures[i].type == "diffuseTexture";
			hasSpecular = hasSpecular || this->textures[i].type == "specularTexture";
		}

		GLuint white = 0;
		if (!hasDiffuse) {
			white = getWhiteTexture();
			glActiveTexture(GL_TEXTURE0 + 6);
			glBindTexture(GL_TEXTURE_2D, white);
			glUniform1i(glGetUniformLocation(shader.shaderProgram, "diffuseTexture"), 6);
		}
		if (!hasSpecular) {
			if (white == 0) white = getWhiteTexture();
			glActiveTexture(GL_TEXTURE0 + 7);
			glBindTexture(GL_TEXTURE_2D, white);
			glUniform1i(glGetUniformLocation(shader.shaderProgram, "specularTexture"), 7);
		}

		glBindVertexArray(this->buffers.VAO);
		glDrawElements(GL_TRIANGLES, (GLsizei)this->indices.size(), GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);

        for(GLuint i = 0; i < this->textures.size(); i++) {

            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

		if (!hasDiffuse) {
			glActiveTexture(GL_TEXTURE0 + 6);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		if (!hasSpecular) {
			glActiveTexture(GL_TEXTURE0 + 7);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		glActiveTexture(GL_TEXTURE0);

    }

	// Initializes all the buffer objects/arrays
	void Mesh::setupMesh() {

		// Create buffers/arrays
		glGenVertexArrays(1, &this->buffers.VAO);
		glGenBuffers(1, &this->buffers.VBO);
		glGenBuffers(1, &this->buffers.EBO);

		glBindVertexArray(this->buffers.VAO);
		// Load data into vertex buffers
		glBindBuffer(GL_ARRAY_BUFFER, this->buffers.VBO);
		glBufferData(GL_ARRAY_BUFFER, this->vertices.size() * sizeof(Vertex), &this->vertices[0], GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->buffers.EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->indices.size() * sizeof(GLuint), &this->indices[0], GL_STATIC_DRAW);

		// Set the vertex attribute pointers
		// Vertex Positions
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)0);
		// Vertex Normals
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)offsetof(Vertex, Normal));
		// Vertex Texture Coords
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)offsetof(Vertex, TexCoords));

		glBindVertexArray(0);
	}
}
