import os
import cv2
import dlib
import numpy as np
import faiss
import pickle

detector = dlib.get_frontal_face_detector()
sp = dlib.shape_predictor("model/shape_predictor_68_face_landmarks.dat")
face_rec_model = dlib.face_recognition_model_v1("model/dlib_face_recognition_resnet_model_v1.dat")

def get_face_descriptor(image_path):
    """Extract face descriptor from input image"""
    try:
        img = cv2.imread(image_path)
            
        scale = 0.5
        width = int(img.shape[1] * scale)
        height = int(img.shape[0] * scale)
        img = cv2.resize(img, (width, height))
        rgb_img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        
        dets = detector(rgb_img)
        shape = sp(rgb_img, dets[0])
        descriptor = face_rec_model.compute_face_descriptor(rgb_img, shape)
        return np.array(descriptor, dtype=np.float32)
    except Exception as e:
        print(f"Error processing input image: {str(e)}")
        return None

def load_cache(cache_file="face_descriptors_cache.pkl"):
    """Load face descriptors from cache"""
    try:
        if not os.path.exists(cache_file):
            print("Cache file not found.")
            return None, None, None
            
        with open(cache_file, 'rb') as f:
            cache_data = pickle.load(f)
            print("Successfully loaded cache data.")
            
            index = faiss.IndexFlatL2(128)
            descriptors_array = np.array(cache_data['descriptors'])
            index.add(descriptors_array)
            
            return index, cache_data['image_paths'], cache_data['labels']
    except Exception as e:
        print(f"Error loading cache: {str(e)}")
        return None, None, None

def predict_face(test_image, index, image_paths, labels):
    """Predict face identity using cached data"""
    try:
        descriptor = get_face_descriptor(test_image)
        if descriptor is None:
            return "No face found in test image."
        
        descriptor = np.expand_dims(descriptor, axis=0)
        distances, indices = index.search(descriptor, 1)
        
        if indices[0][0] == -1:
            return "No matching results found."
            
        distance = distances[0][0]
        confidence = float(1 - distance/2)
            
        if confidence > 0.85:
            matched_path = image_paths[indices[0][0]]
            matched_label = labels[indices[0][0]]
            
            return {
                "status": "success",
                "folder": matched_label,
                "matched_image": matched_path,
                "distance": float(distance),
                "confidence": confidence
            }
        else:
            return f"Confidence too low ({confidence:.2%}). Must be greater than 85%"
        
    except Exception as e:
        return f"Error during prediction: {str(e)}"

if __name__ == "__main__":
    index, image_paths, labels = load_cache()
    
    if index is not None:
        test_image = input("Enter path to test image: ")
        if os.path.exists(test_image):
            result = predict_face(test_image, index, image_paths, labels)
            if isinstance(result, dict):
                print("\nPrediction Results:")
                print(f"Matched Folder: {result['folder']}")
                print(f"Confidence: {result['confidence']:.2%}")
            else:
                print(result)
        else:
            print("Test image not found")
    else:
        print("Failed to load cache data")